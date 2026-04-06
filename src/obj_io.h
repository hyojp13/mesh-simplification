#ifndef MESH_SIMPLIFICATION_OBJ_IO_H_
#define MESH_SIMPLIFICATION_OBJ_IO_H_

#include <filesystem>
#include <iosfwd>

#include "mesh.h"

namespace gfx::obj_io {

Mesh LoadMesh(const std::filesystem::path& obj_filepath);
Mesh LoadMesh(std::istream& input);

void WriteMesh(const std::filesystem::path& obj_filepath, const Mesh& mesh);
void WriteMesh(std::ostream& output, const Mesh& mesh);

}  // namespace gfx::obj_io

#endif  // MESH_SIMPLIFICATION_OBJ_IO_H_
