#include "geometry/mesh_simplifier.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "geometry/face.h"
#include "geometry/half_edge.h"
#include "geometry/half_edge_mesh.h"
#include "geometry/vertex.h"
#include "mesh.h"

#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
#include <omp.h>
#endif

namespace gfx {

namespace {

struct EdgeContraction {
  EdgeContraction(const EdgeKey& edge_key, std::shared_ptr<Vertex> vertex, const float cost)
      : edge_key{edge_key}, vertex{std::move(vertex)}, cost{cost} {}

  EdgeKey edge_key;
  std::shared_ptr<Vertex> vertex;
  float cost;
  bool valid = true;
};

struct Partitioning {
  std::size_t partition_count = 1;
  std::unordered_map<int, std::size_t> vertex_partition;
  std::vector<std::size_t> vertex_counts;
};

struct RawCandidate {
  std::shared_ptr<EdgeContraction> contraction;
  std::size_t partition_index = 0;
  bool is_boundary = false;
};

struct CandidateBuildResult {
  std::vector<std::shared_ptr<EdgeContraction>> sequential_candidates;
  std::vector<std::vector<std::shared_ptr<EdgeContraction>>> partition_candidates;
  std::vector<std::shared_ptr<EdgeContraction>> boundary_candidates;
  std::size_t interior_edge_count = 0;
  std::size_t boundary_edge_count = 0;
};

struct PartitionSelectionResult {
  std::vector<std::shared_ptr<EdgeContraction>> selected_candidates;
  std::size_t rejected_conflicts = 0;
};

struct PhaseAccumulator {
  double half_edge_build_seconds = 0.0;
  double initial_quadrics_seconds = 0.0;
  double initial_queue_build_seconds = 0.0;
  double partition_build_seconds = 0.0;
  double local_selection_seconds = 0.0;
  double boundary_repair_seconds = 0.0;
  double queue_refresh_seconds = 0.0;
};

struct MetricAccumulator {
  std::size_t initial_vertex_count = 0;
  std::size_t initial_face_count = 0;
  std::size_t target_vertex_count = 0;
  std::size_t accepted_collapses = 0;
  std::size_t accepted_local_collapses = 0;
  std::size_t accepted_boundary_collapses = 0;
  std::size_t skipped_boundary_edges = 0;
  std::size_t rejected_conflicts = 0;
  std::size_t repartition_count = 0;
  std::size_t queue_refresh_count = 0;
  double interior_edge_fraction_sum = 0.0;
  double boundary_edge_fraction_sum = 0.0;
  double local_queue_size_sum = 0.0;
  double boundary_queue_size_sum = 0.0;
  double partition_load_imbalance_sum = 0.0;
  double partition_load_imbalance_max = 1.0;
  std::vector<mesh::RoundMetrics> round_metrics;
};

constexpr auto kMinCostComparator = [](const auto& lhs, const auto& rhs) { return lhs->cost > rhs->cost; };

bool CandidateLess(const std::shared_ptr<EdgeContraction>& lhs, const std::shared_ptr<EdgeContraction>& rhs) {
  if (lhs->cost != rhs->cost) return lhs->cost < rhs->cost;
  return lhs->edge_key < rhs->edge_key;
}

int DetermineParallelism(const std::size_t requested_threads) {
  const auto thread_count = std::max<std::size_t>(1, requested_threads);
#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
  return static_cast<int>(thread_count);
#else
  (void)thread_count;
  return 1;
#endif
}

std::shared_ptr<const HalfEdge> GetMinEdge(const std::shared_ptr<const HalfEdge>& edge01) {
  const auto edge10 = std::const_pointer_cast<const HalfEdge>(edge01->flip());
  return edge01->vertex()->id() < edge10->vertex()->id() ? edge01 : edge10;
}

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
  const auto iterator = quadrics.find(v0.id());
  if (iterator == quadrics.end()) {
    throw std::logic_error{"Missing vertex quadric"};
  }
  return iterator->second;
}

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

  return {std::make_shared<Vertex>(position), static_cast<float>(QuadricError(q01, position))};
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

std::vector<std::shared_ptr<const HalfEdge>> CollectCandidateEdges(const HalfEdgeMesh& half_edge_mesh) {
  std::vector<std::shared_ptr<const HalfEdge>> edges;
  edges.reserve(half_edge_mesh.edges().size() / 2);

  for (const auto& edge : half_edge_mesh.edges() | std::views::values) {
    if (!IsTwoSidedEdge(edge)) continue;
    if (edge->vertex()->id() >= edge->flip()->vertex()->id()) continue;
    edges.push_back(edge);
  }

  return edges;
}

std::unordered_map<std::size_t, Mat4> BuildInitialQuadrics(const HalfEdgeMesh& half_edge_mesh, const int parallelism) {
  std::vector<int> vertex_ids;
  vertex_ids.reserve(half_edge_mesh.vertices().size());
  std::unordered_map<int, std::size_t> dense_index;
  dense_index.reserve(half_edge_mesh.vertices().size());
  for (std::size_t dense = 0; const auto& [vertex_id, _vertex] : half_edge_mesh.vertices()) {
    vertex_ids.push_back(vertex_id);
    dense_index.emplace(vertex_id, dense++);
  }

  std::vector<std::shared_ptr<const Face>> faces;
  faces.reserve(half_edge_mesh.faces().size());
  for (const auto& face : half_edge_mesh.faces() | std::views::values) {
    faces.push_back(face);
  }

  std::unordered_map<std::size_t, Mat4> quadrics;
  quadrics.reserve(vertex_ids.size());

#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
  std::vector<std::vector<Mat4>> partial_quadrics(static_cast<std::size_t>(parallelism), std::vector<Mat4>(vertex_ids.size()));

#pragma omp parallel num_threads(parallelism)
  {
    const auto thread_index = static_cast<std::size_t>(omp_get_thread_num());
    auto& local_quadrics = partial_quadrics[thread_index];

#pragma omp for schedule(static)
    for (std::int64_t face_index = 0; face_index < static_cast<std::int64_t>(faces.size()); ++face_index) {
      const auto& face = faces[static_cast<std::size_t>(face_index)];
      const auto v0 = face->try_v0();
      const auto v1 = face->try_v1();
      const auto v2 = face->try_v2();
      if (!v0 || !v1 || !v2) continue;

      const auto& normal = face->normal();
      const Vec4 plane{normal, -Dot(v0->position(), normal)};
      const auto quadric = OuterProduct(plane, plane);

      local_quadrics[dense_index.at(v0->id())] += quadric;
      local_quadrics[dense_index.at(v1->id())] += quadric;
      local_quadrics[dense_index.at(v2->id())] += quadric;
    }
  }

  for (std::size_t dense = 0; dense < vertex_ids.size(); ++dense) {
    Mat4 combined{};
    for (const auto& partial : partial_quadrics) {
      combined += partial[dense];
    }
    quadrics.emplace(static_cast<std::size_t>(vertex_ids[dense]), combined);
  }
#else
  for (const auto& [vertex_id, _vertex] : half_edge_mesh.vertices()) {
    quadrics.emplace(static_cast<std::size_t>(vertex_id), Mat4{});
  }
  for (const auto& face : half_edge_mesh.faces() | std::views::values) {
    AddFaceQuadrics(*face, quadrics);
  }
#endif

  return quadrics;
}

Partitioning BuildPartitioning(const HalfEdgeMesh& half_edge_mesh,
                               std::size_t requested_partitions,
                               const int parallelism) {
  Partitioning partitioning;
  if (half_edge_mesh.vertices().empty()) return partitioning;

  requested_partitions = std::max<std::size_t>(1, requested_partitions);
  partitioning.partition_count = std::min<std::size_t>(requested_partitions, half_edge_mesh.vertices().size());
  partitioning.vertex_counts.assign(partitioning.partition_count, 0);
  partitioning.vertex_partition.reserve(half_edge_mesh.vertices().size());

  std::vector<std::shared_ptr<const Vertex>> vertices;
  vertices.reserve(half_edge_mesh.vertices().size());
  for (const auto& vertex : half_edge_mesh.vertices() | std::views::values) {
    vertices.push_back(vertex);
  }

  const auto& seed_position = vertices.front()->position();
  Vec3 minimum = seed_position;
  Vec3 maximum = seed_position;
  for (const auto& vertex : vertices) {
    const auto& position = vertex->position();
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
  }

  const std::array<double, 3> spans{maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z};
  const auto axis = static_cast<std::size_t>(std::distance(
      spans.begin(), std::max_element(spans.begin(), spans.end())));
  const auto span = std::max(spans[axis], 1.0e-12);

  std::vector<std::size_t> assignments(vertices.size(), 0);
#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
#pragma omp parallel for schedule(static) num_threads(parallelism) if(parallelism > 1)
  for (std::int64_t vertex_index = 0; vertex_index < static_cast<std::int64_t>(vertices.size()); ++vertex_index) {
    const auto& vertex = vertices[static_cast<std::size_t>(vertex_index)];
    const auto coordinate = vertex->position()[axis];
    const auto normalized = std::clamp((coordinate - minimum[axis]) / span, 0.0, 1.0);
    const auto scaled = normalized * static_cast<double>(partitioning.partition_count);
    auto partition_index = static_cast<std::size_t>(scaled);
    if (partition_index >= partitioning.partition_count) {
      partition_index = partitioning.partition_count - 1;
    }
    assignments[static_cast<std::size_t>(vertex_index)] = partition_index;
  }
#else
  for (std::size_t vertex_index = 0; vertex_index < vertices.size(); ++vertex_index) {
    const auto& vertex = vertices[vertex_index];
    const auto coordinate = vertex->position()[axis];
    const auto normalized = std::clamp((coordinate - minimum[axis]) / span, 0.0, 1.0);
    const auto scaled = normalized * static_cast<double>(partitioning.partition_count);
    auto partition_index = static_cast<std::size_t>(scaled);
    if (partition_index >= partitioning.partition_count) {
      partition_index = partitioning.partition_count - 1;
    }
    assignments[vertex_index] = partition_index;
  }
#endif

  for (std::size_t vertex_index = 0; vertex_index < vertices.size(); ++vertex_index) {
    const auto partition_index = assignments[vertex_index];
    partitioning.vertex_partition.emplace(vertices[vertex_index]->id(), partition_index);
    ++partitioning.vertex_counts[partition_index];
  }

  return partitioning;
}

bool ClassifyInteriorEdge(const HalfEdge& edge01, const Partitioning& partitioning, std::size_t& partition_index) {
  const auto edge10 = edge01.flip();
  const auto v0 = edge10->try_vertex();
  const auto v1 = edge01.try_vertex();
  const auto v0_next = edge10->try_next() != nullptr ? edge10->try_next()->try_vertex() : nullptr;
  const auto v1_next = edge01.try_next() != nullptr ? edge01.try_next()->try_vertex() : nullptr;
  if (!v0 || !v1 || !v0_next || !v1_next) return false;

  const auto p0 = partitioning.vertex_partition.find(v0->id());
  const auto p1 = partitioning.vertex_partition.find(v1->id());
  const auto p2 = partitioning.vertex_partition.find(v0_next->id());
  const auto p3 = partitioning.vertex_partition.find(v1_next->id());
  if (p0 == partitioning.vertex_partition.end() || p1 == partitioning.vertex_partition.end() ||
      p2 == partitioning.vertex_partition.end() || p3 == partitioning.vertex_partition.end()) {
    return false;
  }

  if (p0->second == p1->second && p1->second == p2->second && p2->second == p3->second) {
    partition_index = p0->second;
    return true;
  }

  return false;
}

CandidateBuildResult BuildCandidateQueues(const HalfEdgeMesh& half_edge_mesh,
                                         const std::unordered_map<std::size_t, Mat4>& quadrics,
                                         const int parallelism,
                                         const Partitioning* partitioning) {
  const auto edges = CollectCandidateEdges(half_edge_mesh);

  CandidateBuildResult result;
  if (partitioning != nullptr) {
    result.partition_candidates.resize(partitioning->partition_count);
  }

#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
  std::vector<std::vector<RawCandidate>> partial_candidates(static_cast<std::size_t>(parallelism));

#pragma omp parallel num_threads(parallelism)
  {
    const auto thread_index = static_cast<std::size_t>(omp_get_thread_num());
    auto& local_candidates = partial_candidates[thread_index];

#pragma omp for schedule(static)
    for (std::int64_t edge_index = 0; edge_index < static_cast<std::int64_t>(edges.size()); ++edge_index) {
      const auto& edge = edges[static_cast<std::size_t>(edge_index)];
      const auto [vertex, cost] = GetOptimalEdgeContractionVertex(*edge, quadrics);
      auto candidate = std::make_shared<EdgeContraction>(MakeEdgeKey(*GetMinEdge(edge)), vertex, cost);

      RawCandidate raw_candidate{candidate};
      if (partitioning != nullptr) {
        std::size_t partition_index = 0;
        if (ClassifyInteriorEdge(*edge, *partitioning, partition_index)) {
          raw_candidate.partition_index = partition_index;
        } else {
          raw_candidate.is_boundary = true;
        }
      }
      local_candidates.push_back(std::move(raw_candidate));
    }
  }

  for (auto& local_candidates : partial_candidates) {
    for (auto& candidate : local_candidates) {
      if (partitioning == nullptr) {
        result.sequential_candidates.push_back(std::move(candidate.contraction));
        continue;
      }

      if (candidate.is_boundary) {
        result.boundary_candidates.push_back(std::move(candidate.contraction));
        ++result.boundary_edge_count;
      } else {
        result.partition_candidates[candidate.partition_index].push_back(std::move(candidate.contraction));
        ++result.interior_edge_count;
      }
    }
  }
#else
  for (const auto& edge : edges) {
    const auto [vertex, cost] = GetOptimalEdgeContractionVertex(*edge, quadrics);
    auto candidate = std::make_shared<EdgeContraction>(MakeEdgeKey(*GetMinEdge(edge)), vertex, cost);

    if (partitioning == nullptr) {
      result.sequential_candidates.push_back(std::move(candidate));
      continue;
    }

    std::size_t partition_index = 0;
    if (ClassifyInteriorEdge(*edge, *partitioning, partition_index)) {
      result.partition_candidates[partition_index].push_back(std::move(candidate));
      ++result.interior_edge_count;
    } else {
      result.boundary_candidates.push_back(std::move(candidate));
      ++result.boundary_edge_count;
    }
  }
#endif

  std::ranges::sort(result.sequential_candidates, CandidateLess);
  std::ranges::sort(result.boundary_candidates, CandidateLess);
  for (auto& partition_candidates : result.partition_candidates) {
    std::ranges::sort(partition_candidates, CandidateLess);
  }
  if (partitioning == nullptr) {
    result.interior_edge_count = result.sequential_candidates.size();
  }

  return result;
}

double ComputeLoadImbalance(const std::vector<std::vector<std::shared_ptr<EdgeContraction>>>& partition_candidates) {
  if (partition_candidates.empty()) return 1.0;

  std::size_t max_count = 0;
  std::size_t min_count = std::numeric_limits<std::size_t>::max();
  for (const auto& candidates : partition_candidates) {
    max_count = std::max(max_count, candidates.size());
    min_count = std::min(min_count, candidates.size());
  }

  if (max_count == 0) return 1.0;
  if (min_count == 0) return static_cast<double>(max_count);
  return static_cast<double>(max_count) / static_cast<double>(min_count);
}

std::vector<int> CollectConflictVertexIds(const HalfEdgeMesh& half_edge_mesh, const EdgeKey& edge_key) {
  const auto iterator = half_edge_mesh.edges().find(edge_key);
  if (iterator == half_edge_mesh.edges().end()) return {};

  const auto& edge01 = iterator->second;
  if (!IsTwoSidedEdge(edge01)) return {};

  const auto edge10 = edge01->try_flip();
  const auto v0 = edge10->try_vertex();
  const auto v1 = edge01->try_vertex();
  const auto v0_next = edge10->try_next() != nullptr ? edge10->try_next()->try_vertex() : nullptr;
  const auto v1_next = edge01->try_next() != nullptr ? edge01->try_next()->try_vertex() : nullptr;
  if (!v0 || !v1 || !v0_next || !v1_next) return {};

  std::vector<int> vertex_ids{v0->id(), v1->id(), v0_next->id(), v1_next->id()};
  std::ranges::sort(vertex_ids);
  vertex_ids.erase(std::unique(vertex_ids.begin(), vertex_ids.end()), vertex_ids.end());
  return vertex_ids;
}

bool IsCandidateLocallyValid(const HalfEdgeMesh& half_edge_mesh, const EdgeKey& edge_key) {
  const auto live_edge_iterator = half_edge_mesh.edges().find(edge_key);
  if (live_edge_iterator == half_edge_mesh.edges().end()) return false;

  const auto& edge01 = live_edge_iterator->second;
  if (!IsTwoSidedEdge(edge01)) return false;
  if (WillDegenerate(edge01)) return false;

  const auto v0 = edge01->try_flip()->try_vertex();
  const auto v1 = edge01->try_vertex();
  const auto v0_next = edge01->try_flip()->try_next() != nullptr ? edge01->try_flip()->try_next()->try_vertex() : nullptr;
  const auto v1_next = edge01->try_next() != nullptr ? edge01->try_next()->try_vertex() : nullptr;
  if (!v0 || !v1 || !v0_next || !v1_next) return false;

  if (!CanUpdateIncidentEdges(*v0, *v1_next, *v0_next, half_edge_mesh.edges()) ||
      !CanUpdateIncidentEdges(*v1, *v0_next, *v1_next, half_edge_mesh.edges())) {
    return false;
  }

  return ForEachClosedRingEdge(edge01->try_flip(), [](const auto&) {}) && ForEachClosedRingEdge(edge01, [](const auto&) {});
}

PartitionSelectionResult SelectPartitionCandidates(const HalfEdgeMesh& half_edge_mesh,
                                                   const std::vector<std::shared_ptr<EdgeContraction>>& partition_candidates,
                                                   const std::size_t batch_size) {
  auto result = PartitionSelectionResult{};
  result.selected_candidates.reserve(std::min(batch_size, partition_candidates.size()));

  std::unordered_set<int> claimed_vertices;
  for (const auto& candidate : partition_candidates) {
    if (result.selected_candidates.size() >= batch_size) break;
    if (!IsCandidateLocallyValid(half_edge_mesh, candidate->edge_key)) continue;

    const auto conflict_vertex_ids = CollectConflictVertexIds(half_edge_mesh, candidate->edge_key);
    if (conflict_vertex_ids.empty()) continue;

    auto conflicts = false;
    for (const auto vertex_id : conflict_vertex_ids) {
      if (claimed_vertices.contains(vertex_id)) {
        conflicts = true;
        break;
      }
    }
    if (conflicts) {
      ++result.rejected_conflicts;
      continue;
    }

    for (const auto vertex_id : conflict_vertex_ids) {
      claimed_vertices.insert(vertex_id);
    }
    result.selected_candidates.push_back(candidate);
  }

  return result;
}

PartitionSelectionResult SelectPartitionBatch(const HalfEdgeMesh& half_edge_mesh,
                                              const std::vector<std::vector<std::shared_ptr<EdgeContraction>>>& partition_candidates,
                                              const std::size_t batch_size,
                                              const int parallelism) {
  auto per_partition_results = std::vector<PartitionSelectionResult>(partition_candidates.size());

#if defined(MESH_SIMPLIFICATION_HAS_OPENMP)
#pragma omp parallel for schedule(dynamic) num_threads(parallelism) if(parallelism > 1)
  for (std::int64_t partition_index = 0; partition_index < static_cast<std::int64_t>(partition_candidates.size());
       ++partition_index) {
    per_partition_results[static_cast<std::size_t>(partition_index)] = SelectPartitionCandidates(
        half_edge_mesh, partition_candidates[static_cast<std::size_t>(partition_index)], batch_size);
  }
#else
  for (std::size_t partition_index = 0; partition_index < partition_candidates.size(); ++partition_index) {
    per_partition_results[partition_index] =
        SelectPartitionCandidates(half_edge_mesh, partition_candidates[partition_index], batch_size);
  }
#endif

  auto result = PartitionSelectionResult{};
  for (auto& partition_result : per_partition_results) {
    result.rejected_conflicts += partition_result.rejected_conflicts;
    for (auto& candidate : partition_result.selected_candidates) {
      result.selected_candidates.push_back(std::move(candidate));
    }
  }

  std::ranges::sort(result.selected_candidates, CandidateLess);

  // Interior edges should already be partition-disjoint, but keep a deterministic final filter
  // so cross-partition conflicts are handled conservatively.
  std::unordered_set<int> globally_claimed_vertices;
  auto filtered_candidates = std::vector<std::shared_ptr<EdgeContraction>>{};
  filtered_candidates.reserve(result.selected_candidates.size());
  for (const auto& candidate : result.selected_candidates) {
    const auto conflict_vertex_ids = CollectConflictVertexIds(half_edge_mesh, candidate->edge_key);
    if (conflict_vertex_ids.empty()) continue;

    auto conflicts = false;
    for (const auto vertex_id : conflict_vertex_ids) {
      if (globally_claimed_vertices.contains(vertex_id)) {
        conflicts = true;
        break;
      }
    }
    if (conflicts) {
      ++result.rejected_conflicts;
      continue;
    }

    for (const auto vertex_id : conflict_vertex_ids) {
      globally_claimed_vertices.insert(vertex_id);
    }
    filtered_candidates.push_back(candidate);
  }

  result.selected_candidates = std::move(filtered_candidates);
  return result;
}

PartitionSelectionResult SelectBoundaryBatch(const HalfEdgeMesh& half_edge_mesh,
                                             const std::vector<std::shared_ptr<EdgeContraction>>& boundary_candidates,
                                             const std::size_t budget) {
  auto result = PartitionSelectionResult{};
  result.selected_candidates.reserve(std::min(budget, boundary_candidates.size()));

  // Boundary edges are exactly where two or more partitions may want the same neighborhood.
  // Select them with one global claim table rather than per-partition claim tables.
  std::unordered_set<int> claimed_vertices;
  for (const auto& candidate : boundary_candidates) {
    if (result.selected_candidates.size() >= budget) break;
    if (!IsCandidateLocallyValid(half_edge_mesh, candidate->edge_key)) continue;

    const auto conflict_vertex_ids = CollectConflictVertexIds(half_edge_mesh, candidate->edge_key);
    if (conflict_vertex_ids.empty()) continue;

    auto conflicts = false;
    for (const auto vertex_id : conflict_vertex_ids) {
      if (claimed_vertices.contains(vertex_id)) {
        conflicts = true;
        break;
      }
    }

    if (conflicts) {
      ++result.rejected_conflicts;
      continue;
    }

    for (const auto vertex_id : conflict_vertex_ids) {
      claimed_vertices.insert(vertex_id);
    }
    result.selected_candidates.push_back(candidate);
  }

  return result;
}

bool TryContractCandidate(HalfEdgeMesh& half_edge_mesh,
                          std::unordered_map<std::size_t, Mat4>& quadrics,
                          const std::shared_ptr<EdgeContraction>& edge_contraction,
                          std::size_t& next_vertex_id) {
  const auto live_edge_iterator = half_edge_mesh.edges().find(edge_contraction->edge_key);
  if (live_edge_iterator == half_edge_mesh.edges().end()) return false;

  const auto& edge01 = live_edge_iterator->second;
  if (!IsTwoSidedEdge(edge01)) return false;
  if (WillDegenerate(edge01)) return false;

  const auto v0 = edge01->flip()->vertex();
  const auto v1 = edge01->vertex();
  const auto edge10 = edge01->flip();
  const auto v0_next = edge10->next()->vertex();
  const auto v1_next = edge01->next()->vertex();

  if (!CanUpdateIncidentEdges(*v0, *v1_next, *v0_next, half_edge_mesh.edges()) ||
      !CanUpdateIncidentEdges(*v1, *v0_next, *v1_next, half_edge_mesh.edges())) {
    return false;
  }

  if (!ForEachClosedRingEdge(edge10, [](const auto&) {}) || !ForEachClosedRingEdge(edge01, [](const auto&) {})) {
    return false;
  }

  const auto& q0 = GetQuadric(*v0, quadrics);
  const auto& q1 = GetQuadric(*v1, quadrics);

  const auto v_new = std::make_shared<Vertex>(edge_contraction->vertex->position());
  v_new->set_id(static_cast<int>(next_vertex_id++));
  quadrics.emplace(static_cast<std::size_t>(v_new->id()), q0 + q1);

  half_edge_mesh.Contract(*edge01, v_new);
  return true;
}

Mesh RunSequentialSimplification(HalfEdgeMesh& half_edge_mesh,
                                 std::unordered_map<std::size_t, Mat4>& quadrics,
                                 const mesh::SimplifyOptions& options,
                                 PhaseAccumulator& phases,
                                 MetricAccumulator& metrics) {
  auto queue_build_start = std::chrono::high_resolution_clock::now();
  const auto candidates = BuildCandidateQueues(half_edge_mesh, quadrics, DetermineParallelism(options.num_threads), nullptr);
  phases.initial_queue_build_seconds +=
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - queue_build_start).count();

  std::priority_queue<std::shared_ptr<EdgeContraction>,
                      std::vector<std::shared_ptr<EdgeContraction>>,
                      decltype(kMinCostComparator)>
      edge_contractions{kMinCostComparator};
  std::map<EdgeKey, std::shared_ptr<EdgeContraction>> valid_edges;

  for (const auto& candidate : candidates.sequential_candidates) {
    edge_contractions.push(candidate);
    valid_edges.emplace(candidate->edge_key, candidate);
  }

  auto next_vertex_id = half_edge_mesh.vertices().size();
  const auto is_simplified = [&] {
    return edge_contractions.empty() || half_edge_mesh.vertices().size() <= metrics.target_vertex_count;
  };

  for (; !is_simplified(); edge_contractions.pop()) {
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
    quadrics.emplace(static_cast<std::size_t>(v_new->id()), q0 + q1);
    half_edge_mesh.Contract(*edge01, v_new);
    ++metrics.accepted_collapses;

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

    ForEachClosedRingEdge(edgeji, [&](const auto& edgeji_inner) {
      const auto edgeij = edgeji_inner->flip();
      ForEachClosedRingEdge(edgeij, [&](const auto& edgekj) {
        if (!IsTwoSidedEdge(edgekj)) return;

        const auto min_edge = GetMinEdge(edgekj);
        if (const auto min_edge_key = MakeEdgeKey(*min_edge); !visited_edges.contains(min_edge_key)) {
          if (const auto iterator = valid_edges.find(min_edge_key); iterator != valid_edges.end()) {
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

Mesh RunPartitionedBatchSimplification(HalfEdgeMesh& half_edge_mesh,
                                       std::unordered_map<std::size_t, Mat4>& quadrics,
                                       const mesh::SimplifyOptions& options,
                                       PhaseAccumulator& phases,
                                       MetricAccumulator& metrics) {
  const auto requested_partitions =
      std::max<std::size_t>(1, options.num_partitions != 0 ? options.num_partitions : options.num_threads);
  auto partitioning = Partitioning{};
  auto last_round_load_imbalance = 1.0;
  auto accepted_since_repartition = std::size_t{0};
  auto next_vertex_id = half_edge_mesh.vertices().size();
  auto first_queue_build = true;
  auto have_partitioning = false;
  auto last_round_boundary_fraction = 0.0;
  auto last_round_local_acceptance_rate = 1.0;

  while (half_edge_mesh.vertices().size() > metrics.target_vertex_count) {
    const auto repartition_interval = options.repartition_every != 0
                                          ? options.repartition_every
                                          : std::max<std::size_t>(
                                                256,
                                                static_cast<std::size_t>(
                                                    std::ceil(0.005 * static_cast<double>(half_edge_mesh.vertices().size()))));
    // Repartition when the old spatial split has become stale.
    // 1) accepted_since_repartition: topology has changed enough.
    // 2) load imbalance: some partitions are doing much more work than others.
    // 3) boundary fraction: too many edges now straddle partitions, so local work is starved.
    // 4) local acceptance: selected local candidates are mostly becoming invalid before commit.
    const auto should_repartition =
        !have_partitioning || accepted_since_repartition >= repartition_interval || last_round_load_imbalance > 1.5 ||
        last_round_boundary_fraction > 0.35 || last_round_local_acceptance_rate < 0.10;

    auto round_metrics = mesh::RoundMetrics{};
    round_metrics.round = metrics.round_metrics.size();
    round_metrics.repartitioned = should_repartition;

    if (should_repartition) {
      const auto partition_start = std::chrono::high_resolution_clock::now();
      partitioning = BuildPartitioning(half_edge_mesh, requested_partitions, DetermineParallelism(options.num_threads));
      phases.partition_build_seconds +=
          std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - partition_start).count();
      have_partitioning = true;
      accepted_since_repartition = 0;
      ++metrics.repartition_count;
    }

    const auto queue_build_start = std::chrono::high_resolution_clock::now();
    const auto candidate_build =
        BuildCandidateQueues(half_edge_mesh, quadrics, DetermineParallelism(options.num_threads), &partitioning);
    const auto queue_build_seconds =
        std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - queue_build_start).count();
    if (first_queue_build) {
      phases.initial_queue_build_seconds += queue_build_seconds;
      first_queue_build = false;
    } else {
      phases.queue_refresh_seconds += queue_build_seconds;
      ++metrics.queue_refresh_count;
    }

    const auto total_classified_edges = candidate_build.interior_edge_count + candidate_build.boundary_edge_count;
    round_metrics.interior_edge_fraction =
        total_classified_edges == 0
            ? 0.0
            : static_cast<double>(candidate_build.interior_edge_count) / static_cast<double>(total_classified_edges);
    round_metrics.boundary_edge_fraction =
        total_classified_edges == 0
            ? 0.0
            : static_cast<double>(candidate_build.boundary_edge_count) / static_cast<double>(total_classified_edges);
    round_metrics.local_queue_size = static_cast<double>(std::transform_reduce(
        candidate_build.partition_candidates.begin(), candidate_build.partition_candidates.end(), std::size_t{0},
        std::plus<>{}, [](const auto& candidates) { return candidates.size(); }));
    round_metrics.boundary_queue_size = static_cast<double>(candidate_build.boundary_candidates.size());
    round_metrics.partition_load_imbalance = ComputeLoadImbalance(candidate_build.partition_candidates);

    metrics.interior_edge_fraction_sum += round_metrics.interior_edge_fraction;
    metrics.boundary_edge_fraction_sum += round_metrics.boundary_edge_fraction;
    metrics.local_queue_size_sum += round_metrics.local_queue_size;
    metrics.boundary_queue_size_sum += round_metrics.boundary_queue_size;
    metrics.partition_load_imbalance_sum += round_metrics.partition_load_imbalance;
    metrics.partition_load_imbalance_max =
        std::max(metrics.partition_load_imbalance_max, round_metrics.partition_load_imbalance);
    last_round_load_imbalance = round_metrics.partition_load_imbalance;

    if (candidate_build.partition_candidates.empty() && candidate_build.boundary_candidates.empty()) {
      metrics.round_metrics.push_back(round_metrics);
      break;
    }

    const auto selection_start = std::chrono::high_resolution_clock::now();
    auto selection = SelectPartitionBatch(half_edge_mesh, candidate_build.partition_candidates, options.batch_size,
                                          DetermineParallelism(options.num_threads));
    phases.local_selection_seconds +=
        std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - selection_start).count();
    metrics.rejected_conflicts += selection.rejected_conflicts;
    round_metrics.rejected_conflicts += static_cast<double>(selection.rejected_conflicts);

    for (const auto& candidate : selection.selected_candidates) {
      if (half_edge_mesh.vertices().size() <= metrics.target_vertex_count) break;
      if (TryContractCandidate(half_edge_mesh, quadrics, candidate, next_vertex_id)) {
        ++metrics.accepted_collapses;
        ++metrics.accepted_local_collapses;
        ++round_metrics.accepted_local_collapses;
        ++accepted_since_repartition;
      }
    }

    if (half_edge_mesh.vertices().size() <= metrics.target_vertex_count) {
      metrics.round_metrics.push_back(round_metrics);
      break;
    }

    const auto boundary_budget = 16 * partitioning.partition_count;
    const auto boundary_start = std::chrono::high_resolution_clock::now();
    auto accepted_boundary = std::size_t{0};
    auto skipped_boundary = std::size_t{0};

    auto boundary_selection = SelectBoundaryBatch(half_edge_mesh, candidate_build.boundary_candidates, boundary_budget);
    metrics.rejected_conflicts += boundary_selection.rejected_conflicts;
    round_metrics.rejected_conflicts += static_cast<double>(boundary_selection.rejected_conflicts);

    for (const auto& candidate : boundary_selection.selected_candidates) {
      if (half_edge_mesh.vertices().size() <= metrics.target_vertex_count) break;
      if (TryContractCandidate(half_edge_mesh, quadrics, candidate, next_vertex_id)) {
        ++accepted_boundary;
        ++metrics.accepted_collapses;
        ++metrics.accepted_boundary_collapses;
        ++round_metrics.accepted_boundary_collapses;
        ++accepted_since_repartition;
      } else {
        // A candidate can still become stale because local commits happened before the boundary pass.
        ++skipped_boundary;
        ++metrics.skipped_boundary_edges;
        ++round_metrics.skipped_boundary_edges;
      }
    }
    phases.boundary_repair_seconds +=
        std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - boundary_start).count();

    const auto selected_local_count = std::max<std::size_t>(1, selection.selected_candidates.size());
    last_round_boundary_fraction = round_metrics.boundary_edge_fraction;
    last_round_local_acceptance_rate =
        static_cast<double>(round_metrics.accepted_local_collapses) / static_cast<double>(selected_local_count);

    metrics.round_metrics.push_back(round_metrics);

    if (round_metrics.accepted_local_collapses == 0.0 && accepted_boundary == 0) {
      break;
    }
  }

  return static_cast<Mesh>(half_edge_mesh);
}

mesh::SimplificationStats BuildStats(const mesh::SimplifyOptions& options,
                                     const HalfEdgeMesh& half_edge_mesh,
                                     const PhaseAccumulator& phases,
                                     const MetricAccumulator& metrics) {
  auto stats = mesh::SimplificationStats{};
  stats.phase_timings = {
      {"half_edge_build_seconds", phases.half_edge_build_seconds},
      {"initial_quadrics_seconds", phases.initial_quadrics_seconds},
      {"initial_queue_build_seconds", phases.initial_queue_build_seconds},
      {"partition_build_seconds", phases.partition_build_seconds},
      {"local_selection_seconds", phases.local_selection_seconds},
      {"boundary_repair_seconds", phases.boundary_repair_seconds},
      {"queue_refresh_seconds", phases.queue_refresh_seconds},
  };

  const auto round_count = metrics.round_metrics.size();
  const auto safe_divide = [&](const double value) {
    return round_count == 0 ? 0.0 : value / static_cast<double>(round_count);
  };

  stats.summary_metrics = {
      {"initial_vertex_count", static_cast<double>(metrics.initial_vertex_count)},
      {"initial_face_count", static_cast<double>(metrics.initial_face_count)},
      {"target_vertex_count", static_cast<double>(metrics.target_vertex_count)},
      {"final_vertex_count", static_cast<double>(half_edge_mesh.vertices().size())},
      {"final_face_count", static_cast<double>(half_edge_mesh.faces().size())},
      {"requested_threads", static_cast<double>(options.num_threads)},
      {"requested_partitions", static_cast<double>(options.mode == mesh::SimplificationMode::kPartitionedBatch
                                                       ? (options.num_partitions != 0 ? options.num_partitions
                                                                                      : options.num_threads)
                                                       : 1)},
      {"accepted_collapses", static_cast<double>(metrics.accepted_collapses)},
      {"accepted_local_collapses", static_cast<double>(metrics.accepted_local_collapses)},
      {"accepted_boundary_collapses", static_cast<double>(metrics.accepted_boundary_collapses)},
      {"skipped_boundary_edges", static_cast<double>(metrics.skipped_boundary_edges)},
      {"rejected_conflicts", static_cast<double>(metrics.rejected_conflicts)},
      {"repartition_count", static_cast<double>(metrics.repartition_count)},
      {"queue_refresh_count", static_cast<double>(metrics.queue_refresh_count)},
      {"round_count", static_cast<double>(round_count)},
      {"interior_edge_fraction_avg", safe_divide(metrics.interior_edge_fraction_sum)},
      {"boundary_edge_fraction_avg", safe_divide(metrics.boundary_edge_fraction_sum)},
      {"local_queue_size_avg", safe_divide(metrics.local_queue_size_sum)},
      {"boundary_queue_size_avg", safe_divide(metrics.boundary_queue_size_sum)},
      {"partition_load_imbalance_avg", safe_divide(metrics.partition_load_imbalance_sum)},
      {"partition_load_imbalance_max", metrics.partition_load_imbalance_max},
  };
  stats.round_metrics = metrics.round_metrics;
  return stats;
}

}  // namespace

std::string_view mesh::ToString(const SimplificationMode mode) noexcept {
  switch (mode) {
    case SimplificationMode::kSequential:
      return "sequential";
    case SimplificationMode::kPartitionedBatch:
      return "partitioned_batch";
  }
  return "unknown";
}

mesh::SimplifyResult mesh::Simplify(const Mesh& mesh, const SimplifyOptions& options) {
  if (options.target_vertex_fraction < 0.0 || options.target_vertex_fraction > 1.0) {
    throw std::invalid_argument{"Invalid mesh simplification target vertex fraction"};
  }

  auto phases = PhaseAccumulator{};
  auto metrics = MetricAccumulator{};
  metrics.initial_vertex_count = mesh.positions().size();
  metrics.initial_face_count = mesh.indices().size() / 3;
  metrics.target_vertex_count =
      std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(options.target_vertex_fraction * mesh.positions().size())));

  const auto build_start = std::chrono::high_resolution_clock::now();
  auto half_edge_mesh = HalfEdgeMesh{mesh};
  phases.half_edge_build_seconds +=
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - build_start).count();

  const auto quadrics_start = std::chrono::high_resolution_clock::now();
  auto quadrics = BuildInitialQuadrics(half_edge_mesh, DetermineParallelism(options.num_threads));
  phases.initial_quadrics_seconds +=
      std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - quadrics_start).count();

  Mesh simplified_mesh = options.mode == SimplificationMode::kPartitionedBatch
                             ? RunPartitionedBatchSimplification(half_edge_mesh, quadrics, options, phases, metrics)
                             : RunSequentialSimplification(half_edge_mesh, quadrics, options, phases, metrics);

  return {simplified_mesh, BuildStats(options, half_edge_mesh, phases, metrics)};
}

Mesh mesh::Simplify(const Mesh& mesh, const double target_vertex_fraction, const std::size_t num_threads) {
  auto options = SimplifyOptions{};
  options.target_vertex_fraction = target_vertex_fraction;
  options.num_threads = num_threads;
  return Simplify(mesh, options).mesh;
}

}  // namespace gfx
