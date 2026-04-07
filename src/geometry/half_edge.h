#ifndef GEOMETRY_HALF_EDGE_H_
#define GEOMETRY_HALF_EDGE_H_

#include <cassert>
#include <memory>

#include "geometry/face.h"
#include "geometry/vertex.h"

namespace gfx {

/** @brief A directional edge in a half-edge mesh. */
class HalfEdge {
public:
  /**
   * @brief Creates a half-edge.
   * @param vertex The vertex the half-edge will point to.
   */
  explicit HalfEdge(const std::shared_ptr<Vertex>& vertex) noexcept : vertex_{vertex} {}

  /** @brief Gets the vertex at the head of this half-edge. */
  [[nodiscard]] std::shared_ptr<Vertex> vertex() const noexcept {
    assert(!vertex_.expired());
    return vertex_.lock();
  }

  /** @brief Gets the vertex at the head of this half-edge if it is still available. */
  [[nodiscard]] std::shared_ptr<Vertex> try_vertex() const noexcept { return vertex_.lock(); }

  /** @brief Gets the half-edge that shares this edge's vertices in the opposite direction. */
  [[nodiscard]] std::shared_ptr<HalfEdge> flip() const noexcept {
    assert(!flip_.expired());
    return flip_.lock();
  }

  /** @brief Gets the flip half-edge if it is still available. */
  [[nodiscard]] std::shared_ptr<HalfEdge> try_flip() const noexcept { return flip_.lock(); }

  /** @brief Sets the flip half-edge. */
  void set_flip(const std::shared_ptr<HalfEdge>& flip) noexcept {
    assert(*this != *flip);
    assert(next_.expired() || *flip != *next_.lock());
    flip_ = flip;
  }

  /** @brief Gets the next half-edge of a triangle in counter-clockwise order. */
  [[nodiscard]] std::shared_ptr<HalfEdge> next() const noexcept {
    assert(!next_.expired());
    return next_.lock();
  }

  /** @brief Gets the next half-edge if it is still available. */
  [[nodiscard]] std::shared_ptr<HalfEdge> try_next() const noexcept { return next_.lock(); }

  /** @brief Sets the next half-edge. */
  void set_next(const std::shared_ptr<HalfEdge>& next) noexcept {
    assert(*this != *next);
    assert(flip_.expired() || *next != *flip_.lock());
    next_ = next;
  }

  /** @brief Gets the triangle face formed by three counter-clockwise iterations of @ref next. */
  [[nodiscard]] std::shared_ptr<Face> face() const noexcept {
    assert(!face_.expired());
    return face_.lock();
  }

  /** @brief Gets the face if it is still available. */
  [[nodiscard]] std::shared_ptr<Face> try_face() const noexcept { return face_.lock(); }

  /** @brief Sets the half-edge face. */
  void set_face(const std::shared_ptr<Face>& face) noexcept {
    face_ = face;
  }

  /** @brief Defines the half-edge equality operator. */
  friend bool operator==(const HalfEdge& lhs, const HalfEdge& rhs) noexcept {
    return lhs.vertex() == rhs.vertex() && lhs.flip()->vertex() == rhs.flip()->vertex();
  }

  /** @brief Gets a canonical key for the half-edge. */
  friend EdgeKey MakeEdgeKey(const HalfEdge& edge) noexcept { return MakeEdgeKey(*edge.flip()->vertex(), *edge.vertex()); }

private:
  std::weak_ptr<Vertex> vertex_;
  std::weak_ptr<HalfEdge> next_, flip_;
  std::weak_ptr<Face> face_;
};

// defined here to avoid cyclical dependency
inline void Vertex::set_edge(const std::shared_ptr<const HalfEdge>& edge) noexcept {
  assert(*this == *edge->vertex());
  edge_ = edge;
}

/** @brief A type alias for a std::shared_ptr<HalfEdge>. */
using SharedHalfEdge = std::shared_ptr<HalfEdge>;

}  // namespace gfx

#endif  // GEOMETRY_HALF_EDGE_H_
