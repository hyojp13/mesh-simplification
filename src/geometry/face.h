#ifndef GEOMETRY_FACE_H_
#define GEOMETRY_FACE_H_

#include <cassert>
#include <memory>

#include "math3d.h"
#include "geometry/vertex.h"

namespace gfx {

/** @brief A triangle face defined by three vertices in counter-clockwise winding order. */
class Face {
public:
  /**
   * @brief Creates a triangle face.
   * @param v0,v1,v2 The face vertices.
   */
  Face(const std::shared_ptr<const Vertex>& v0,
       const std::shared_ptr<const Vertex>& v1,
       const std::shared_ptr<const Vertex>& v2);

  /** @brief Gets the first face vertex. */
  [[nodiscard]] std::shared_ptr<const Vertex> v0() const noexcept {
    assert(!v0_.expired());
    return v0_.lock();
  }

  /** @brief Gets the second face vertex. */
  [[nodiscard]] std::shared_ptr<const Vertex> v1() const noexcept {
    assert(!v1_.expired());
    return v1_.lock();
  }

  /** @brief Gets the third face vertex. */
  [[nodiscard]] std::shared_ptr<const Vertex> v2() const noexcept {
    assert(!v2_.expired());
    return v2_.lock();
  }

  /** @brief Gets the face normal. */
  [[nodiscard]] const Vec3& normal() const noexcept { return normal_; }

  /** @brief Gets the face area. */
  [[nodiscard]] double area() const noexcept { return area_; }

  /** @brief Defines the face equality operator. */
  friend bool operator==(const Face& lhs, const Face& rhs) noexcept {
    return lhs.v0() == rhs.v0() && lhs.v1() == rhs.v1() && lhs.v2() == rhs.v2();
  }

  /** @brief Gets a canonical key for the face. */
  friend FaceKey MakeFaceKey(const Face& face) noexcept { return MakeFaceKey(*face.v0(), *face.v1(), *face.v2()); }

private:
  std::weak_ptr<const Vertex> v0_, v1_, v2_;
  Vec3 normal_;
  double area_;
};

/** @brief A type alias for a std::shared_ptr<Face>. */
using SharedFace = std::shared_ptr<Face>;

}  // namespace gfx

#endif  // GEOMETRY_FACE_H_
