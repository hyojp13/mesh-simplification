#include "geometry/mesh_simplifier.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geometry/face.h"
#include "geometry/half_edge.h"
#include "geometry/half_edge_mesh.h"
#include "geometry/vertex.h"
#include "mesh.h"

namespace gfx {

namespace {

/** @brief Represents a candidate edge contraction. */
struct EdgeContraction {
  EdgeContraction(const EdgeKey& edge_key, std::shared_ptr<Vertex> vertex, const float cost)
      : edge_key{edge_key}, vertex{std::move(vertex)}, cost{cost} {}

  /** @brief The canonical key of the edge to contract. */
  EdgeKey edge_key;

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
  const auto edge10 = std::const_pointer_cast<const HalfEdge>(edge01->flip());
  return edge01->vertex()->id() < edge10->vertex()->id() ? edge01 : edge10;
}

/** @brief Accumulates a face's error quadric into its incident vertices. */
void AddFaceQuadrics(const Face& face, std::unordered_map<std::size_t, Mat4>& quadrics) {
  const auto v0 = face.try_v0();
  const auto v1 = face.try_v1();
  const auto v2 = face.try_v2();
  if (!v0 || !v1 || !v2) return;

  const auto& normal = face.normal();
  const Vec4 plane{normal, -Dot(v0->position(), normal)};
  const auto quadric = OuterProduct(plane, plane);

  quadrics[static_cast<std::size_t>(v0->id())] += quadric;
  quadrics[static_cast<std::size_t>(v1->id())] += quadric;
  quadrics[static_cast<std::size_t>(v2->id())] += quadric;
}

/** @brief Checks whether a half-edge has the local triangle links needed by this simplifier. */
bool HasTriangleLinks(const std::shared_ptr<const HalfEdge>& edge) {
  return edge != nullptr && edge->try_vertex() != nullptr && edge->try_flip() != nullptr && edge->try_next() != nullptr &&
         edge->try_face() != nullptr;
}

/** @brief Checks whether a half-edge has faces on both sides. */
bool IsTwoSidedEdge(const std::shared_ptr<const HalfEdge>& edge) {
  const auto flip = edge != nullptr ? edge->try_flip() : nullptr;
  return HasTriangleLinks(edge) && HasTriangleLinks(flip);
}

/** @brief Checks whether HalfEdgeMesh::Contract can safely update one side of an edge contraction. */
bool CanUpdateIncidentEdges(const Vertex& v_target,
                            const Vertex& v_start,
                            const Vertex& v_end,
                            const std::map<EdgeKey, std::shared_ptr<HalfEdge>>& edges) {
  const auto edge_start_iterator = edges.find(MakeEdgeKey(v_target, v_start));
  const auto edge_end_iterator = edges.find(MakeEdgeKey(v_target, v_end));
  if (edge_start_iterator == edges.end() || edge_end_iterator == edges.end()) return false;

  const auto& edge_end = edge_end_iterator->second;
  std::vector<const HalfEdge*> visited_edges;
  for (std::shared_ptr<const HalfEdge> edge0i = edge_start_iterator->second; edge0i != edge_end;) {
    if (!IsTwoSidedEdge(edge0i)) return false;
    if (std::ranges::find(visited_edges, edge0i.get()) != visited_edges.end()) return false;
    visited_edges.push_back(edge0i.get());

    const auto edgeij = edge0i->try_next();
    if (!IsTwoSidedEdge(edgeij)) return false;

    const auto edgej0 = edgeij->try_next();
    if (!IsTwoSidedEdge(edgej0)) return false;

    edge0i = edgej0->try_flip();
  }

  return IsTwoSidedEdge(edge_end);
}

/**
 * @brief Iterates over the closed one-ring reached by repeatedly following next()->flip().
 * @return @c false if the local topology is open or corrupted.
 */
template <typename Func>
bool ForEachClosedRingEdge(const std::shared_ptr<const HalfEdge>& start_edge, Func&& func) {
  if (!IsTwoSidedEdge(start_edge)) return false;

  std::vector<const HalfEdge*> visited_edges;
  for (auto edge = start_edge;;) {
    if (!IsTwoSidedEdge(edge)) return false;
    if (std::ranges::find(visited_edges, edge.get()) != visited_edges.end()) return false;

    visited_edges.push_back(edge.get());
    func(edge);

    edge = edge->try_next()->try_flip();
    if (edge == start_edge) return true;
  }
}

/** @brief Gets the error quadric for a given vertex. */
const Mat4& GetQuadric(const Vertex& v0, const std::unordered_map<std::size_t, Mat4>& quadrics) {
  const auto q0_iterator = quadrics.find(v0.id());
  if (q0_iterator == quadrics.end()) {
    throw std::logic_error{"Missing vertex quadric"};
  }
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
  const auto edge10 = edge01->try_flip();
  if (!IsTwoSidedEdge(edge01) || !IsTwoSidedEdge(edge10)) return true;

  const auto v0 = edge10->try_vertex();
  const auto v1_next = edge01->try_next()->try_vertex();
  const auto v0_next = edge10->try_next()->try_vertex();
  if (!v0 || !v1_next || !v0_next) return true;

  std::unordered_map<std::size_t, std::shared_ptr<Vertex>> neighborhood;

  std::vector<const HalfEdge*> visited_edges;
  for (auto iterator = edge01->try_next(); iterator != edge10;) {
    if (!IsTwoSidedEdge(iterator)) return true;
    if (std::ranges::find(visited_edges, iterator.get()) != visited_edges.end()) return true;
    visited_edges.push_back(iterator.get());

    if (const auto vertex = iterator->try_vertex(); vertex && vertex != v0 && vertex != v1_next && vertex != v0_next) {
      neighborhood.emplace(vertex->id(), vertex);
    }

    iterator = iterator->try_flip()->try_next();
  }

  visited_edges.clear();
  for (auto iterator = edge10->try_next(); iterator != edge01;) {
    if (!IsTwoSidedEdge(iterator)) return true;
    if (std::ranges::find(visited_edges, iterator.get()) != visited_edges.end()) return true;
    visited_edges.push_back(iterator.get());

    if (const auto vertex = iterator->try_vertex(); vertex && neighborhood.contains(vertex->id())) {
      return true;
    }

    iterator = iterator->try_flip()->try_next();
  }

  return false;
}

}  // namespace

Mesh mesh::Simplify(const Mesh& mesh,
                    const double target_vertex_fraction,
                    [[maybe_unused]] const std::size_t num_threads) {
  if (target_vertex_fraction < 0.0f || target_vertex_fraction > 1.0f) {
    throw std::invalid_argument{"Invalid mesh simplification target vertex fraction"};
  }

  HalfEdgeMesh half_edge_mesh{mesh};

  // Compute error quadrics from faces instead of walking vertex rings; imported meshes may have boundaries.
  std::unordered_map<std::size_t, Mat4> quadrics;
  for (const auto& [vertex_id, vertex] : half_edge_mesh.vertices()) {
    quadrics.emplace(vertex_id, Mat4{});
  }
  for (const auto& face : half_edge_mesh.faces() | std::views::values) {
    AddFaceQuadrics(*face, quadrics);
  }

  // use a priority queue to sort edge contraction candidates by the cost of removing each edge
  static constexpr auto kMinCostComparator = [](const auto& lhs, const auto& rhs) { return lhs->cost > rhs->cost; };
  std::priority_queue<std::shared_ptr<EdgeContraction>,
                      std::vector<std::shared_ptr<EdgeContraction>>,
                      decltype(kMinCostComparator)>
      edge_contractions{kMinCostComparator};

  // this is used to invalidate existing priority queue entries as edges are updated or removed from the mesh
  std::map<EdgeKey, std::shared_ptr<EdgeContraction>> valid_edges;

  // compute the optimal vertex position that minimizes the cost of contracting each valid interior edge
  for (const auto& edge : half_edge_mesh.edges() | std::views::values) {
    if (!IsTwoSidedEdge(edge)) continue;

    const auto min_edge = GetMinEdge(edge);

    if (const auto min_edge_key = MakeEdgeKey(*min_edge); !valid_edges.contains(min_edge_key)) {
      const auto [vertex, cost] = GetOptimalEdgeContractionVertex(*edge, quadrics);
      const auto edge_contraction = std::make_shared<EdgeContraction>(min_edge_key, vertex, cost);
      edge_contractions.push(edge_contraction);
      valid_edges.emplace(min_edge_key, edge_contraction);
    }
  }

  // stop mesh simplification when the target number of vertices remain
  const auto initial_vertex_count = half_edge_mesh.vertices().size();
  const auto target_vertex_count =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(target_vertex_fraction * initial_vertex_count)));
  const auto is_simplified = [&] {
    return edge_contractions.empty() || half_edge_mesh.vertices().size() <= target_vertex_count;
  };

  for (auto next_vertex_id = half_edge_mesh.vertices().size(); !is_simplified(); edge_contractions.pop()) {
    const auto& edge_contraction = edge_contractions.top();
    if (!edge_contraction->valid) continue;

    const auto live_edge_iterator = half_edge_mesh.edges().find(edge_contraction->edge_key);
    if (live_edge_iterator == half_edge_mesh.edges().end()) continue;

    const auto& edge01 = live_edge_iterator->second;

    if (!IsTwoSidedEdge(edge01)) continue;
    if (WillDegenerate(edge01)) continue;

    const auto v0 = edge01->flip()->vertex();
    const auto v1 = edge01->vertex();
    const auto edge10 = edge01->flip();
    const auto v0_next = edge10->next()->vertex();
    const auto v1_next = edge01->next()->vertex();

    if (!CanUpdateIncidentEdges(*v0, *v1_next, *v0_next, half_edge_mesh.edges()) ||
        !CanUpdateIncidentEdges(*v1, *v0_next, *v1_next, half_edge_mesh.edges())) {
      continue;
    }

    if (!ForEachClosedRingEdge(edge10, [](const auto&) {}) || !ForEachClosedRingEdge(edge01, [](const auto&) {})) {
      continue;
    }

    const auto& q0 = GetQuadric(*v0, quadrics);
    const auto& q1 = GetQuadric(*v1, quadrics);

    // invalidate entries in the priority queue that will be removed during the edge contraction
    for (const auto& start_edge : std::array<std::shared_ptr<const HalfEdge>, 2>{edge10, edge01}) {
      ForEachClosedRingEdge(start_edge, [&](const auto& edgeji) {
        const auto min_edge = GetMinEdge(edgeji);
        if (const auto iterator = valid_edges.find(MakeEdgeKey(*min_edge)); iterator != valid_edges.end()) {
          iterator->second->valid = false;
          valid_edges.erase(iterator);
        }
      });
    }

    // only assign a new vertex ID when processing the next edge contraction
    const auto& v_new = edge_contraction->vertex;
    v_new->set_id(static_cast<int>(next_vertex_id++));

    // compute the error quadric for the new vertex
    quadrics.emplace(v_new->id(), q0 + q1);

    // remove the edge from the mesh and attach incident edges to the new vertex
    half_edge_mesh.Contract(*edge01, v_new);

    // add new edge contraction candidates for edges affected by the edge contraction
    std::map<EdgeKey, std::shared_ptr<const HalfEdge>> visited_edges;
    const auto& vi = v_new;
    auto edgeji = vi->try_edge();
    if (!IsTwoSidedEdge(edgeji)) {
      edgeji.reset();
      for (const auto& edge : half_edge_mesh.edges() | std::views::values) {
        if (edge->try_vertex() == vi && IsTwoSidedEdge(edge)) {
          vi->set_edge(edge);
          edgeji = edge;
          break;
        }
      }
    }
    if (!edgeji) continue;

    ForEachClosedRingEdge(edgeji, [&](const auto& edgeji) {
      const auto edgeij = edgeji->flip();
      ForEachClosedRingEdge(edgeij, [&](const auto& edgekj) {
        if (!IsTwoSidedEdge(edgekj)) return;

        const auto min_edge = GetMinEdge(edgekj);
        if (const auto min_edge_key = MakeEdgeKey(*min_edge); !visited_edges.contains(min_edge_key)) {
          if (const auto iterator = valid_edges.find(min_edge_key); iterator != valid_edges.end()) {
            // invalidate existing edge contraction candidate in the priority queue
            iterator->second->valid = false;
          }
          const auto [new_vertex, new_cost] = GetOptimalEdgeContractionVertex(*min_edge, quadrics);
          const auto new_edge_contraction = std::make_shared<EdgeContraction>(min_edge_key, new_vertex, new_cost);
          valid_edges[min_edge_key] = new_edge_contraction;
          edge_contractions.push(new_edge_contraction);
          visited_edges.emplace(min_edge_key, min_edge);
        }
      });
    });
  }

  return static_cast<Mesh>(half_edge_mesh);
}

}  // namespace gfx
