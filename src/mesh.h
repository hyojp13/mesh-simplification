#ifndef MESH_SIMPLIFICATION_MESH_H_
#define MESH_SIMPLIFICATION_MESH_H_

#include <cstdint>
#include <span>
#include <vector>

#include "math3d.h"

namespace gfx {

using Index = std::uint32_t;

class Mesh {
public:
  explicit Mesh(std::span<const Vec3> positions, std::span<const Index> indices = {});

  [[nodiscard]] const std::vector<Vec3>& positions() const noexcept { return positions_; }
  [[nodiscard]] const std::vector<Index>& indices() const noexcept { return indices_; }

private:
  std::vector<Vec3> positions_;
  std::vector<Index> indices_;
};

}  // namespace gfx

#endif  // MESH_SIMPLIFICATION_MESH_H_
