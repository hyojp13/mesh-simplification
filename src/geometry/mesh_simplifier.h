#ifndef GEOMETRY_MESH_SIMPLIFIER_H_
#define GEOMETRY_MESH_SIMPLIFIER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "mesh.h"

namespace gfx {
namespace mesh {

enum class SimplificationMode {
  kSequential,
  kPartitionedBatch,
};

[[nodiscard]] std::string_view ToString(SimplificationMode mode) noexcept;

struct SimplifyOptions {
  double target_vertex_fraction = 1.0;
  std::size_t num_threads = 1;
  SimplificationMode mode = SimplificationMode::kSequential;
  std::size_t num_partitions = 0;
  std::size_t batch_size = 64;
  std::size_t repartition_every = 0;
};

struct PhaseTiming {
  std::string name;
  double seconds = 0.0;
};

struct SummaryMetric {
  std::string name;
  double value = 0.0;
};

struct RoundMetrics {
  std::size_t round = 0;
  double interior_edge_fraction = 0.0;
  double boundary_edge_fraction = 0.0;
  double partition_load_imbalance = 1.0;
  double local_queue_size = 0.0;
  double boundary_queue_size = 0.0;
  double accepted_local_collapses = 0.0;
  double accepted_boundary_collapses = 0.0;
  double skipped_boundary_edges = 0.0;
  double rejected_conflicts = 0.0;
  bool repartitioned = false;
};

struct SimplificationStats {
  std::vector<PhaseTiming> phase_timings;
  std::vector<SummaryMetric> summary_metrics;
  std::vector<RoundMetrics> round_metrics;
};

struct SimplifyResult {
  Mesh mesh;
  SimplificationStats stats;
};

/**
 * @brief Reduces the number of vertices in a mesh.
 * @param mesh The mesh to simplify.
 * @param options The requested simplification options.
 * @return A triangle mesh simplified to approximately @p options.target_vertex_fraction of the input vertices,
 *         plus phase timings and experiment metrics.
 * @throw std::invalid_argument Thrown if the target fraction is not in the interval [0,1].
 */
SimplifyResult Simplify(const Mesh& mesh, const SimplifyOptions& options);

/**
 * @brief Compatibility overload for the original CLI path.
 * @param mesh The mesh to simplify.
 * @param target_vertex_fraction The fraction of the original vertices to keep.
 * @param num_threads The requested thread count.
 * @return A triangle mesh simplified to approximately @p target_vertex_fraction of the input vertices.
 */
Mesh Simplify(const Mesh& mesh, double target_vertex_fraction, std::size_t num_threads);

}  // namespace mesh
}  // namespace gfx

#endif  // GEOMETRY_MESH_SIMPLIFIER_H_
