#ifndef GRAPHICS_ARC_CAMERA_H_
#define GRAPHICS_ARC_CAMERA_H_

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace gfx {

struct ViewFrustum {
  float field_of_view_y = 0.0f;
  float aspect_ratio = 0.0f;
  float z_near = 0.0f;
  float z_far = 0.0f;
};

struct SphericalCoordinates {
  float radius = 0.0f;
  float theta = 0.0f;
  float phi = 0.0f;
};

class ArcCamera {
public:
  ArcCamera(const glm::vec3& position, const glm::vec3& target, const ViewFrustum& view_frustum);

  void set_aspect_ratio(float aspect_ratio) noexcept;

  [[nodiscard]] const glm::mat4& view_transform() const;
  [[nodiscard]] const glm::mat4& projection_transform() const;

  void Translate(float dx, float dy, float dz);
  void Rotate(float theta, float phi);
  void Zoom(float rate);

private:
  SphericalCoordinates position_;
  glm::vec3 target_{0.0f};
  ViewFrustum view_frustum_;
  mutable std::optional<glm::mat4> view_transform_;
  mutable std::optional<glm::mat4> projection_transform_;
};

}  // namespace gfx

#endif  // GRAPHICS_ARC_CAMERA_H_
