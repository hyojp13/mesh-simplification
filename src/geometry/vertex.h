#ifndef GEOMETRY_VERTEX_H_
#define GEOMETRY_VERTEX_H_

#include <cassert>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include "math3d.h"

namespace gfx {
class HalfEdge;

using EdgeKey = std::pair<int, int>;
using FaceKey = std::tuple<int, int, int>;

/** @brief A half-edge mesh vertex. */
class Vertex {
public:
  /**
   * @brief Creates a vertex.
   * @param position The vertex position.
   */
  explicit Vertex(const Vec3& position) noexcept : position_{position} {}

  /**
   * @brief Creates a vertex.
   * @param id The vertex ID.
   * @param position The vertex position.
   */
  Vertex(const int id, const Vec3& position) noexcept : id_{id}, position_{position} {}

  /** @brief Gets the vertex ID. */
  [[nodiscard]] int id() const noexcept {
    assert(id_.has_value());
    return *id_;
  }

  /** @brief Sets the vertex ID. */
  void set_id(const int id) noexcept { id_ = id; }

  /** @brief Gets the vertex position. */
  [[nodiscard]] const Vec3& position() const noexcept { return position_; }

  /** @brief Gets the last created half-edge that points to this vertex. */
  [[nodiscard]] std::shared_ptr<const HalfEdge> edge() const noexcept {
    assert(!edge_.expired());
    return edge_.lock();
  }

  /** @brief Sets the vertex half-edge. */
  void set_edge(const std::shared_ptr<const HalfEdge>& edge) noexcept;

  /** @brief Defines the vertex equality operator. */
  friend bool operator==(const Vertex& lhs, const Vertex& rhs) noexcept { return lhs.id() == rhs.id(); }

private:
  std::optional<int> id_;
  Vec3 position_;
  std::weak_ptr<const HalfEdge> edge_;
};

[[nodiscard]] inline EdgeKey MakeEdgeKey(const Vertex& v0, const Vertex& v1) noexcept { return {v0.id(), v1.id()}; }

[[nodiscard]] inline FaceKey MakeFaceKey(const Vertex& v0, const Vertex& v1, const Vertex& v2) noexcept {
  return {v0.id(), v1.id(), v2.id()};
}

/** @brief A type alias for a std::shared_ptr<Vertex>. */
using SharedVertex = std::shared_ptr<Vertex>;

}  // namespace gfx

#endif  // GEOMETRY_VERTEX_H_
