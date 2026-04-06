#include "mesh.h"

#include <stdexcept>

namespace gfx {

namespace {

void Validate(std::span<const Vec3> positions, std::span<const Index> indices) {
  if (positions.empty()) {
    throw std::invalid_argument{"Vertex positions must be specified"};
  }

  if (indices.size() % 3 != 0) {
    throw std::invalid_argument{"Face indices must define a triangle mesh"};
  }

  for (const auto index : indices) {
    if (index >= positions.size()) {
      throw std::invalid_argument{"Face index is out of bounds"};
    }
  }
}

}  // namespace

Mesh::Mesh(const std::span<const Vec3> positions, const std::span<const Index> indices)
    : positions_{positions.begin(), positions.end()}, indices_{indices.begin(), indices.end()} {
  Validate(positions_, indices_);
}

}  // namespace gfx
