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
#include <immintrin.h> // Required for AVX/SIMD intrinsics
#include <chrono>      // Required for profiling
#include <iostream>    // Required for profiling output

#include "geometry/face.h"
#include "geometry/half_edge.h"
#include "geometry/half_edge_mesh.h"
#include "geometry/vertex.h"
#include "mesh.h"

namespace gfx {

namespace {

// ============================================================================
// SIMD Matrix Struct (Embedded)
// ============================================================================
struct alignas(32) SimdMat4d {
  union {
    __m256d rows[4];
    double data[4][4];
  };

  SimdMat4d() {
    rows[0] = _mm256_setzero_pd();
    rows[1] = _mm256_setzero_pd();
    rows[2] = _mm256_setzero_pd();
    rows[3] = _mm256_setzero_pd();
  }

  explicit SimdMat4d(const double* values) {
    rows[0] = _mm256_loadu_pd(&values[0]);
    rows[1] = _mm256_loadu_pd(&values[4]);
    rows[2] = _mm256_loadu_pd(&values[8]);
    rows[3] = _mm256_loadu_pd(&values[12]);
  }

  // SIMD Matrix Addition
  SimdMat4d operator+(const SimdMat4d& other) const {
    SimdMat4d result;
    result.rows[0] = _mm256_add_pd(rows[0], other.rows[0]);
    result.rows[1] = _mm256_add_pd(rows[1], other.rows[1]);
    result.rows[2] = _mm256_add_pd(rows[2], other.rows[2]);
    result.rows[3] = _mm256_add_pd(rows[3], other.rows[3]);
    return result;
  }

  // SIMD Calculation for v^T * Q * v (Quadric Error)
  double ComputeQuadricError(const Vec3& pos) const {
    // 1. Create a vector v = [x, y, z, 1.0]
    __m256d v = _mm256_setr_pd(pos.x, pos.y, pos.z, 1.0);

    // 2. Compute w = Q * v using Fused Multiply-Add (FMA)
    __m256d v_x = _mm256_set1_pd(pos.x);
    __m256d v_y = _mm256_set1_pd(pos.y);
    __m256d v_z = _mm256_set1_pd(pos.z);
    __m256d v_w = _mm256_set1_pd(1.0);

    __m256d w = _mm256_mul_pd(v_x, rows[0]);
    w = _mm256_fmadd_pd(v_y, rows[1], w);
    w = _mm256_fmadd_pd(v_z, rows[2], w);
    w = _mm256_fmadd_pd(v_w, rows[3], w);

    // 3. Compute the final dot product E = v . w
    __m256d vw = _mm256_mul_pd(v, w);
    __m256d sum1 = _mm256_hadd_pd(vw, vw); 
    __m256d sum2 = _mm256_add_pd(sum1, _mm256_permute2f128_pd(sum1, sum1, 1)); 
    
    return _mm256_cvtsd_f64(sum2);
  }

  // Extract back to standard Mat4
  void Store(Mat4& out_mat) const {
    alignas(32) double temp[16];
    _mm256_storeu_pd(&temp[0], rows[0]);
    _mm256_storeu_pd(&temp[4], rows[1]);
    _mm256_storeu_pd(&temp[8], rows[2]);
    _mm256_storeu_pd(&temp[12], rows[3]);
    out_mat = *reinterpret_cast<Mat4*>(temp);
  }
};
// ============================================================================


/** @brief Represents a candidate edge contraction. */
struct EdgeContraction {
  EdgeContraction(const EdgeKey& edge_key, std::shared_ptr<Vertex> vertex, const float cost)
      : edge_key{edge_key}, vertex{std::move(vertex)}, cost{cost} {}

  EdgeKey edge_key;
  std::shared_ptr<Vertex> vertex;
  float cost;
  bool valid = true;
};

std::shared_ptr<const HalfEdge> GetMinEdge(const std::shared_ptr<const HalfEdge>& edge01) {
  const auto edge10 = std::const_pointer_cast<const HalfEdge>(edge01->flip());
  return edge01->vertex()->id() < edge10->vertex()->id() ? edge01 : edge10;
}

/** @brief Accumulates a face's error quadric into its incident vertices. */
void AddFaceQuadrics(const Face& face, std::unordered_map<std::size_t, Mat4>& quadrics, bool use_simd) {
  const auto v0 = face.try_v0();
  const auto v1 = face.try_v1();
  const auto v2 = face.try_v2();
  if (!v0 || !v1 || !v2) return;

  const auto& normal = face.normal();
  const Vec4 plane{normal, -Dot(v0->position(), normal)};
  
  Mat4 quadric;
  if (use_simd) {
      double p[4] = {plane.x, plane.y, plane.z, plane.w};
      alignas(32) double q_data[16];
      for (int i = 0; i < 4; ++i) {
          __m256d p_i = _mm256_set1_pd(p[i]);
          __m256d p_row = _mm256_loadu_pd(p);
          _mm256_storeu_pd(&q_data[i * 4], _mm256_mul_pd(p_i, p_row));
      }
      SimdMat4d simd_q(q_data);
      simd_q.Store(quadric);
  } else {
      quadric = OuterProduct(plane, plane);
  }

  quadrics[static_cast<std::size_t>(v0->id())] += quadric;
  quadrics[static_cast<std::size_t>(v1->id())] += quadric;
  quadrics[static_cast<std::size_t>(v2->id())] += quadric;
}

bool HasTriangleLinks(const std::shared_ptr<const HalfEdge>& edge) {
  return edge != nullptr && edge->try_vertex() != nullptr && edge->try_flip() != nullptr && edge->try_next() != nullptr &&
         edge->try_face() != nullptr;
}

bool IsTwoSidedEdge(const std::shared_ptr<const HalfEdge>& edge) {
  const auto flip = edge != nullptr ? edge->try_flip() : nullptr;
  return HasTriangleLinks(edge) && HasTriangleLinks(flip);
}

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

const Mat4& GetQuadric(const Vertex& v0, const std::unordered_map<std::size_t, Mat4>& quadrics) {
  const auto q0_iterator = quadrics.find(v0.id());
  if (q0_iterator == quadrics.end()) {
    throw std::logic_error{"Missing vertex quadric"};
  }
  return q0_iterator->second;
}

std::pair<std::shared_ptr<Vertex>, float> GetOptimalEdgeContractionVertex(
    const HalfEdge& edge01,
    const std::unordered_map<std::size_t, Mat4>& quadrics,
    bool use_simd,
    double& time_matrix_add,
    double& time_quadric_error) {
  const auto v0 = edge01.flip()->vertex();
  const auto v1 = edge01.vertex();

  const auto& q0 = GetQuadric(*v0, quadrics);
  const auto& q1 = GetQuadric(*v1, quadrics);

  Mat4 q01;
  float error_cost = 0.0f;
  auto position = (v0->position() + v1->position()) / 2.0;

  auto t_add_start = std::chrono::high_resolution_clock::now();
  if (use_simd) {
      SimdMat4d simd_q0(reinterpret_cast<const double*>(&q0));
      SimdMat4d simd_q1(reinterpret_cast<const double*>(&q1));
      SimdMat4d simd_q01 = simd_q0 + simd_q1;
      simd_q01.Store(q01);
  } else {
      q01 = q0 + q1;
  }
  time_matrix_add += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t_add_start).count();

  if (static constexpr auto kEpsilon = 1.0e-8; std::fabs(Determinant(UpperLeft3x3(q01))) >= kEpsilon) {
    if (const auto optimal_position = SolveLinearSystem(UpperLeft3x3(q01), -RightColumnXYZ(q01), kEpsilon);
        optimal_position.has_value()) {
      position = *optimal_position;
    }
  }

  auto t_err_start = std::chrono::high_resolution_clock::now();
  if (use_simd) {
      SimdMat4d simd_q01(reinterpret_cast<const double*>(&q01));
      error_cost = static_cast<float>(simd_q01.ComputeQuadricError(position));
  } else {
      error_cost = static_cast<float>(QuadricError(q01, position));
  }
  time_quadric_error += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t_err_start).count();

  return std::pair{std::make_shared<Vertex>(position), error_cost};
}

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
                    [[maybe_unused]] const std::size_t num_threads,
                    bool use_simd) {
  if (target_vertex_fraction < 0.0f || target_vertex_fraction > 1.0f) {
    throw std::invalid_argument{"Invalid mesh simplification target vertex fraction"};
  }

  double time_add_face_quadrics = 0.0;
  double time_matrix_add = 0.0;
  double time_quadric_error = 0.0;

  HalfEdgeMesh half_edge_mesh{mesh};

  std::unordered_map<std::size_t, Mat4> quadrics;
  for (const auto& [vertex_id, vertex] : half_edge_mesh.vertices()) {
    quadrics.emplace(vertex_id, Mat4{});
  }
  
  auto t_face_start = std::chrono::high_resolution_clock::now();
  for (const auto& face : half_edge_mesh.faces() | std::views::values) {
    AddFaceQuadrics(*face, quadrics, use_simd);
  }
  time_add_face_quadrics = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t_face_start).count();

  static constexpr auto kMinCostComparator = [](const auto& lhs, const auto& rhs) { return lhs->cost > rhs->cost; };
  std::priority_queue<std::shared_ptr<EdgeContraction>,
                      std::vector<std::shared_ptr<EdgeContraction>>,
                      decltype(kMinCostComparator)>
      edge_contractions{kMinCostComparator};

  std::map<EdgeKey, std::shared_ptr<EdgeContraction>> valid_edges;

  for (const auto& edge : half_edge_mesh.edges() | std::views::values) {
    if (!IsTwoSidedEdge(edge)) continue;

    const auto min_edge = GetMinEdge(edge);

    if (const auto min_edge_key = MakeEdgeKey(*min_edge); !valid_edges.contains(min_edge_key)) {
      const auto [vertex, cost] = GetOptimalEdgeContractionVertex(*edge, quadrics, use_simd, time_matrix_add, time_quadric_error);
      const auto edge_contraction = std::make_shared<EdgeContraction>(min_edge_key, vertex, cost);
      edge_contractions.push(edge_contraction);
      valid_edges.emplace(min_edge_key, edge_contraction);
    }
  }

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

    for (const auto& start_edge : std::array<std::shared_ptr<const HalfEdge>, 2>{edge10, edge01}) {
      ForEachClosedRingEdge(start_edge, [&](const auto& edgeji) {
        const auto min_edge = GetMinEdge(edgeji);
        if (const auto iterator = valid_edges.find(MakeEdgeKey(*min_edge)); iterator != valid_edges.end()) {
          iterator->second->valid = false;
          valid_edges.erase(iterator);
        }
      });
    }

    const auto& v_new = edge_contraction->vertex;
    v_new->set_id(static_cast<int>(next_vertex_id++));

    auto t_add_collapse = std::chrono::high_resolution_clock::now();
    if (use_simd) {
        SimdMat4d simd_q0(reinterpret_cast<const double*>(&q0));
        SimdMat4d simd_q1(reinterpret_cast<const double*>(&q1));
        Mat4 q_sum;
        (simd_q0 + simd_q1).Store(q_sum);
        quadrics.emplace(v_new->id(), q_sum);
    } else {
        quadrics.emplace(v_new->id(), q0 + q1);
    }
    time_matrix_add += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - t_add_collapse).count();

    half_edge_mesh.Contract(*edge01, v_new);

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
            iterator->second->valid = false;
          }
          const auto [new_vertex, new_cost] = GetOptimalEdgeContractionVertex(*min_edge, quadrics, use_simd, time_matrix_add, time_quadric_error);
          const auto new_edge_contraction = std::make_shared<EdgeContraction>(min_edge_key, new_vertex, new_cost);
          valid_edges[min_edge_key] = new_edge_contraction;
          edge_contractions.push(new_edge_contraction);
          visited_edges.emplace(min_edge_key, min_edge);
        }
      });
    });
  }

  std::cout << "    -> Time [Face Quadric Setup]: " << time_add_face_quadrics << " s\n";
  std::cout << "    -> Time [Matrix Additions]:   " << time_matrix_add << " s\n";
  std::cout << "    -> Time [Quadric Errors]:     " << time_quadric_error << " s\n";

  return static_cast<Mesh>(half_edge_mesh);
}

}  // namespace gfx