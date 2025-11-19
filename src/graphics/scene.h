#ifndef GRAPHICS_SCENE_H_
#define GRAPHICS_SCENE_H_

#include <filesystem>

#include "graphics/arc_camera.h"
#include "graphics/mesh.h"
#include "graphics/shader_program.h"

namespace gfx {

/** @brief A lightweight abstraction for a 3D mesh viewer. */
class Scene {
public:
  /**
   * Creates a scene.
   * @param obj_filepath The .obj filepath of the triangle mesh to load.
   * @param aspect_ratio The camera aspect ratio.
   */
  Scene(const std::filesystem::path& obj_filepath, float aspect_ratio);

  /** @brief Gets an arc camera that rotates around the mesh to view. */
  [[nodiscard]] ArcCamera& camera() noexcept { return camera_; }

  /** @brief Gets the triangle mesh being viewed in the scene. */
  [[nodiscard]] Mesh& mesh() noexcept { return mesh_; }

  /** @brief Renders the scene to the current framebuffer. */
  void Render() const;

private:
  ArcCamera camera_;
  Mesh mesh_;
  ShaderProgram shader_program_;
};

}  // namespace gfx

#endif  // GRAPHICS_SCENE_H_
