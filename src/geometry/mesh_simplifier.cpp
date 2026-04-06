#include "geometry/mesh_simplifier.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geometry/half_edge.h"
#include "geometry/half_edge_mesh.h"
#include "geometry/vertex.h"
#include "mesh.h"

namespace gfx {

namespace {

/** @brief Represents a candidate edge contraction. */
struct EdgeContraction {
  EdgeContraction(std::shared_ptr<const HalfEdge> edge, std::shared_ptr<Vertex> vertex, const float cost)
      : edge{std::move(edge)}, vertex{std::move(vertex)}, cost{cost} {}

  /** @brief The edge to contract. */
  std::shared_ptr<const HalfEdge> edge;

  /** @brief The optimal vertex position that minimizes the cost of this edge contraction. */
  std::shared_ptr<Vertex> vertex;

  /** @brief A metric that quantifies how much the mesh will change after this edge has been contracted. */
  float cost;

  /**
   * @brief This is used as a workaround for priority_queue not providing a method to update an existing
   *        entry's priority. As edges are updated in the mesh, duplicated entries may be inserted in the queue
   *        and this property will be used to determine if an entry refers to the most recent edge update.
   */
  bool valid = true;
};

/**
 * @brief Gets a canonical representation of a half-edge used to disambiguate between its flip edge.
 * @param edge01 The half-edge to disambiguate.
 * @return For two vertices connected by an edge, returns the half-edge pointing to the vertex with the smallest ID.
 */
std::shared_ptr<const HalfEdge> GetMinEdge(const std::shared_ptr<const HalfEdge>& edge01) {
  const auto edge10 = const_pointer_cast<const HalfEdge>(edge01->flip());
  return edge01->vertex()->id() < edge10->vertex()->id() ? edge01 : edge10;
}

/** @brief Computes the error quadric for a vertex. */
Mat4 ComputeQuadric(const Vertex& v0) {
  Mat4 quadric{};
  auto edgei0 = v0.edge();
  do {
    const auto& position = v0.position();
    const auto& normal = edgei0->face()->normal();
    const Vec4 plane{normal, -Dot(position, normal)};
    quadric += OuterProduct(plane, plane);
    edgei0 = edgei0->next()->flip();
  } while (edgei0 != v0.edge());
  return quadric;
}

/** @brief Gets the error quadric for a given vertex. */
const Mat4& GetQuadric(const Vertex& v0, const std::unordered_map<std::size_t, Mat4>& quadrics) {
  const auto q0_iterator = quadrics.find(v0.id());
  assert(q0_iterator != quadrics.end());
  return q0_iterator->second;
}

/**
 * @brief Determines the optimal vertex position for an edge contraction.
 * @param edge01 The edge to evaluate.
 * @param quadrics A mapping of error quadrics by vertex ID.
 * @return The optimal vertex and cost associated with contracting @p edge01.
 */
std::pair<std::shared_ptr<Vertex>, float> GetOptimalEdgeContractionVertex(
    const HalfEdge& edge01,
    const std::unordered_map<std::size_t, Mat4>& quadrics) {
  const auto v0 = edge01.flip()->vertex();
  const auto v1 = edge01.vertex();

  const auto& q0 = GetQuadric(*v0, quadrics);
  const auto& q1 = GetQuadric(*v1, quadrics);

  const auto q01 = q0 + q1;
  auto position = (v0->position() + v1->position()) / 2.0;

  if (static constexpr auto kEpsilon = 1.0e-8; std::fabs(Determinant(UpperLeft3x3(q01))) >= kEpsilon) {
    if (const auto optimal_position = SolveLinearSystem(UpperLeft3x3(q01), -RightColumnXYZ(q01), kEpsilon);
        optimal_position.has_value()) {
      position = *optimal_position;
    }
  }

  return std::pair{std::make_shared<Vertex>(position), static_cast<float>(QuadricError(q01, position))};
}

/**
 * @brief Determines if the removal of an edge will cause the mesh to degenerate.
 * @param edge01 The edge to evaluate.
 * @return @c true if the removal of @p edge01 will produce a non-manifold, otherwise @c false.
 */
bool WillDegenerate(const std::shared_ptr<const HalfEdge>& edge01) {
  const auto v0 = edge01->flip()->vertex();
  const auto v1_next = edge01->next()->vertex();
  const auto v0_next = edge01->flip()->next()->vertex();
  std::unordered_map<std::size_t, std::shared_ptr<Vertex>> neighborhood;

  for (auto iterator = edge01->next(); iterator != edge01->flip(); iterator = iterator->flip()->next()) {
    if (const auto vertex = iterator->vertex(); vertex != v0 && vertex != v1_next && vertex != v0_next) {
      neighborhood.emplace(vertex->id(), vertex);
    }
  }

  for (auto iterator = edge01->flip()->next(); iterator != edge01; iterator = iterator->flip()->next()) {
    if (const auto vertex = iterator->vertex(); neighborhood.contains(vertex->id())) {
      return true;
    }
  }

  return false;
}

}  // namespace

Mesh mesh::Simplify(const Mesh& mesh, const double rate) {
  if (rate < 0.0f || rate > 1.0f) {
    throw std::invalid_argument{"Invalid mesh simplification rate"};
  }

  const auto start_time = std::chrono::high_resolution_clock::now();
  HalfEdgeMesh half_edge_mesh{mesh};

  // compute error quadrics for each vertex
  std::unordered_map<std::size_t, Mat4> quadrics;
  for (const auto& [vertex_id, vertex] : half_edge_mesh.vertices()) {
    quadrics.emplace(vertex_id, ComputeQuadric(*vertex));
  }

  // use a priority queue to sort edge contraction candidates by the cost of removing each edge
  static constexpr auto kMinCostComparator = [](const auto& lhs, const auto& rhs) { return lhs->cost > rhs->cost; };
  std::priority_queue<std::shared_ptr<EdgeContraction>,
                      std::vector<std::shared_ptr<EdgeContraction>>,
                      decltype(kMinCostComparator)>
      edge_contractions{kMinCostComparator};

  // this is used to invalidate existing priority queue entries as edges are updated or removed from the mesh
  std::map<EdgeKey, std::shared_ptr<EdgeContraction>> valid_edges;

  // compute the optimal vertex position that minimizes the cost of contracting each edge
  for (const auto& edge : half_edge_mesh.edges() | std::views::values) {
    const auto min_edge = GetMinEdge(edge);

    if (const auto min_edge_key = MakeEdgeKey(*min_edge); !valid_edges.contains(min_edge_key)) {
      const auto [vertex, cost] = GetOptimalEdgeContractionVertex(*edge, quadrics);
      const auto edge_contraction = std::make_shared<EdgeContraction>(edge, vertex, cost);
      edge_contractions.push(edge_contraction);
      valid_edges.emplace(min_edge_key, edge_contraction);
    }
  }

  // stop mesh simplification if the number of triangles has been sufficiently reduced
  const auto initial_face_count = half_edge_mesh.faces().size();
  const auto target_face_count = static_cast<std::size_t>(std::floor((1.0 - rate) * initial_face_count));
  const auto is_simplified = [&] {
    return edge_contractions.empty() || half_edge_mesh.faces().size() <= target_face_count;
  };

  for (auto next_vertex_id = half_edge_mesh.vertices().size(); !is_simplified(); edge_contractions.pop()) {
    const auto& edge_contraction = edge_contractions.top();
    const auto& edge01 = edge_contraction->edge;

    if (!edge_contraction->valid || WillDegenerate(edge01)) continue;

    const auto v0 = edge01->flip()->vertex();
    const auto v1 = edge01->vertex();

    const auto& q0 = GetQuadric(*v0, quadrics);
    const auto& q1 = GetQuadric(*v1, quadrics);

    // only assign a new vertex ID when processing the next edge contraction
    const auto& v_new = edge_contraction->vertex;
    v_new->set_id(static_cast<int>(next_vertex_id++));

    // compute the error quadric for the new vertex
    quadrics.emplace(v_new->id(), q0 + q1);

    // invalidate entries in the priority queue that will be removed during the edge contraction
    for (const auto& vi : {v0, v1}) {
      auto edgeji = vi->edge();
      do {
        const auto min_edge = GetMinEdge(edgeji);
        if (const auto iterator = valid_edges.find(MakeEdgeKey(*min_edge)); iterator != valid_edges.end()) {
          iterator->second->valid = false;
          valid_edges.erase(iterator);
        }
        edgeji = edgeji->next()->flip();
      } while (edgeji != vi->edge());
    }

    // remove the edge from the mesh and attach incident edges to the new vertex
    half_edge_mesh.Contract(*edge01, v_new);

    // add new edge contraction candidates for edges affected by the edge contraction
    std::map<EdgeKey, std::shared_ptr<const HalfEdge>> visited_edges;
    const auto& vi = v_new;
    auto edgeji = vi->edge();
    do {
      const auto vj = edgeji->flip()->vertex();
      auto edgekj = vj->edge();
      do {
        const auto min_edge = GetMinEdge(edgekj);
        if (const auto min_edge_key = MakeEdgeKey(*min_edge); !visited_edges.contains(min_edge_key)) {
          if (const auto iterator = valid_edges.find(min_edge_key); iterator != valid_edges.end()) {
            // invalidate existing edge contraction candidate in the priority queue
            iterator->second->valid = false;
          }
          const auto [new_vertex, new_cost] = GetOptimalEdgeContractionVertex(*min_edge, quadrics);
          const auto new_edge_contraction = std::make_shared<EdgeContraction>(min_edge, new_vertex, new_cost);
          valid_edges[min_edge_key] = new_edge_contraction;
          edge_contractions.push(new_edge_contraction);
          visited_edges.emplace(min_edge_key, min_edge);
        }
        edgekj = edgekj->next()->flip();
      } while (edgekj != vj->edge());
      edgeji = edgeji->next()->flip();
    } while (edgeji != vi->edge());
  }

  std::clog << "Mesh simplified from " << initial_face_count << " to " << half_edge_mesh.faces().size()
            << " triangles in "
            << std::chrono::duration<double>{std::chrono::high_resolution_clock::now() - start_time}.count()
            << " seconds\n";

  return static_cast<Mesh>(half_edge_mesh);
}

}  // namespace gfx
