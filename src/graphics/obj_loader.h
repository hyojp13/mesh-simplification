#ifndef GRAPHICS_OBJ_LOADER_H_
#define GRAPHICS_OBJ_LOADER_H_

#include <filesystem>

namespace gfx {
class Mesh;

namespace obj_loader {

/**
 * @brief Loads a triangle mesh from an .obj file.
 * @param obj_filepath The .obj filepath for the triangle mesh to load.
 * @return A mesh defined by the position, normals, texture coordinates, and indices specified in the .obj file.
 * @throw std::runtime_error Thrown if the file cannot be opened.
 * @throw std::invalid_argument Thrown if the .obj file is malformed or contains an unsupported format.
 * @note At this time, only a subset of the .obj file specification is supported which includes 3D vertex positions,
 *       3D normals, and 2D texture coordinates. Face elements are supported and may optionally contain normals, texture
 *       coordinates, and indices.
 * @see https://en.wikipedia.org/wiki/Wavefront_.obj_file
 */
Mesh LoadMesh(const std::filesystem::path& obj_filepath);

}  // namespace obj_loader
}  // namespace gfx

#endif  // GRAPHICS_OBJ_LOADER_H_
