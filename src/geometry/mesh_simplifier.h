#ifndef GEOMETRY_MESH_SIMPLIFIER_H_
#define GEOMETRY_MESH_SIMPLIFIER_H_

namespace gfx {
class Mesh;

namespace mesh {

/**
 * @brief Reduces the number of triangles in a mesh.
 * @param mesh The mesh to simplify.
 * @param rate The percentage of triangles to be removed (e.g., .95 indicates 95% of triangles should be removed).
 * @return A triangle mesh with @p rate percent of triangles removed from @p mesh.
 * @throw std::invalid_argument Thrown if the simplification rate is not in the interval [0,1].
 */
Mesh Simplify(const Mesh& mesh, double rate);

}  // namespace mesh
}  // namespace gfx

#endif  // GEOMETRY_MESH_SIMPLIFIER_H_
