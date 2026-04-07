#ifndef GEOMETRY_MESH_SIMPLIFIER_H_
#define GEOMETRY_MESH_SIMPLIFIER_H_

#include <cstddef>

namespace gfx {
class Mesh;

namespace mesh {

/**
 * @brief Reduces the number of vertices in a mesh.
 * @param mesh The mesh to simplify.
 * @param target_vertex_fraction The fraction of the original vertices to keep.
 * @param num_threads The requested thread count. Currently accepted for plumbing but not used.
 * @return A triangle mesh simplified to approximately @p target_vertex_fraction of the input vertices.
 * @throw std::invalid_argument Thrown if the target fraction is not in the interval [0,1].
 */
Mesh Simplify(const Mesh& mesh, double target_vertex_fraction, std::size_t num_threads);

}  // namespace mesh
}  // namespace gfx

#endif  // GEOMETRY_MESH_SIMPLIFIER_H_
