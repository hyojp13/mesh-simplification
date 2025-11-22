#ifndef GRAPHICS_ARC_CAMERA_H_
#define GRAPHICS_ARC_CAMERA_H_

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace gfx {

/** @brief A view frustum for perspective projection. */
struct ViewFrustum {
  /** @brief The vertical field of view in radians. */
  float field_of_view_y = 0.0f;

  /** @brief The ratio of the view width divided by its height.  */
  float aspect_ratio = 0.0f;

  /** @brief The distance to the z-near plane from the camera origin. */
  float z_near = 0.0f;

  /** @brief The distance to the z-far plane from the camera origin. */
  float z_far = 0.0f;
};

/** @brief A camera that rotates around a fixed target. */
class ArcCamera {
public:
  /**
   * @brief Spherical coordinates that define the camera position relative to its target.
   * @details Spherical coordinates follow a right-handed coordinate system convention with horizontal and vertical
   *          offsets starting from the +z-axis.
   */
  struct SphericalCoordinates {
    /** @brief The distance between the camera and its target. */
    float radius = 0.0f;

    /** @brief The horizontal angle of the camera starting from the +z-axis. */
    float theta = 0.0f;

    /** @brief The vertical angle of the camera starting from the +z-axis. */
    float phi = 0.0f;
  };

  /**
   * @brief Creates an @ref ArcCamera.
   * @param position The camera position in world space.
   * @param target The camera target in world space.
   * @param view_frustum The view frustum to enable perspective projection.
   */
  ArcCamera(const glm::vec3& position, const glm::vec3& target, const ViewFrustum& view_frustum);

  /** @brief Sets the aspect ratio for the camera view frustum. */
  void set_aspect_ratio(float aspect_ratio) noexcept;

  /** @brief Gets the view transform matrix for converting world-space vertex positions to view-space coordinates. */
  [[nodiscard]] const glm::mat4& view_transform() const;

  /** @brief Gets the projection transform matrix for converting view-space position to clip-space coordinates. */
  [[nodiscard]] const glm::mat4& projection_transform() const;

  /**
   * @brief Translates the camera along its coordinate axes.
   * @param view_translation The amount to incrementally translate the camera in the (x,y,z) directions in view space.
   */
  void Translate(const glm::vec3& view_translation);

  /**
   * @brief Rotates the camera around its target position.
   * @param theta The horizontal angle in radians to rotate the camera.
   * @param phi The vertical angle in radians to rotate the camera.
   */
  void Rotate(float theta, float phi);

  /**
   * @brief Translates the camera along the forward direction axis aligned with the camera target.
   * @param rate The amount to incrementally translate the camera along its forward direction axis.
   */
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
