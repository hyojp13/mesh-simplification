#ifndef GRAPHICS_SHADER_PROGRAM_H_
#define GRAPHICS_SHADER_PROGRAM_H_

#include <concepts>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <GL/gl3w.h>
#include <glm/gtc/type_ptr.hpp>

namespace gfx {

/** @brief An OpenGL shader program.  */
class ShaderProgram {
public:
  /**
   * @brief Creates a shader program.
   * @param vertex_shader_filepath The vertex shader filepath.
   * @param fragment_shader_filepath The fragment shader filepath.
   * @throw std::runtime_error Thrown if the shader program could not be created.
   */
  ShaderProgram(const std::filesystem::path& vertex_shader_filepath,
                const std::filesystem::path& fragment_shader_filepath);

  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;

  ShaderProgram(ShaderProgram&&) noexcept = delete;
  ShaderProgram& operator=(ShaderProgram&&) noexcept = delete;

  ~ShaderProgram() noexcept { glDeleteProgram(id_); }

  /** @brief Binds the shader program to the current OpenGL context. */
  void Enable() const noexcept { glUseProgram(id_); }

  /**
   * @brief Sets a uniform variable in the shader program.
   * @tparam T The uniform variable type.
   * @param name The uniform variable name.
   * @param value The uniform variable value.
   */
  template <typename T>
  void SetUniform(const std::string_view name, const T& value) const {
    const auto uniform_location = GetUniformLocation(name);

    if constexpr (std::integral<T>) {
      glUniform1i(uniform_location, static_cast<GLint>(value));
    } else if constexpr (std::floating_point<T>) {
      glUniform1f(uniform_location, static_cast<GLfloat>(value));
    } else if constexpr (std::same_as<T, glm::vec3>) {
      glUniform3fv(uniform_location, 1, glm::value_ptr(value));
    } else if constexpr (std::same_as<T, glm::vec4>) {
      glUniform4fv(uniform_location, 1, glm::value_ptr(value));
    } else if constexpr (std::same_as<T, glm::mat3>) {
      glUniformMatrix3fv(uniform_location, 1, GL_FALSE, glm::value_ptr(value));
    } else if constexpr (std::same_as<T, glm::mat4>) {
      glUniformMatrix4fv(uniform_location, 1, GL_FALSE, glm::value_ptr(value));
    } else {
      static_assert(kAssertFalse<T>, "Unsupported uniform variable type");
    }
  }

private:
  /** @brief An OpenGL shader. */
  class Shader {
  public:
    /**
     * @brief Creates a shader.
     * @param shader_type The OpenGL shader type (e.g., GL_FRAGMENT_SHADER).
     * @param shader_source The GLSL shader source code.
     * @throw std::runtime_error Thrown if the shader could not be created.
     */
    Shader(GLenum shader_type, const std::string& shader_source);

    ~Shader() noexcept { glDeleteShader(id); }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&&) noexcept = delete;
    Shader& operator=(Shader&&) noexcept = delete;

    GLuint id;  // NOLINT(misc-non-private-member-variables-in-classes): allow direct access to internal shader class
  };

  // this is required to enable heterogeneous lookup which allows searching a std::unordered_map<std::string, T> using
  // std::string_view keys without unnecessary allocations from constructing a temporary std::string
  struct StringViewHash {
    using is_transparent = void;
    static constexpr std::hash<std::string_view> kStringViewHash;

    std::size_t operator()(const std::string_view value) const noexcept { return kStringViewHash(value); }
  };

  // this is required as a workaround to ensure that static assertions in "if constexpr" expressions are well-formed
  template <typename>
  static constexpr std::false_type kAssertFalse{};

  /**
   * @brief Gets the location for a uniform variable in the shader program.
   * @param name The uniform variable name.
   * @return An integer representing the uniform variable location. Returns -1 if the variable is not active.
   */
  [[nodiscard]] GLint GetUniformLocation(std::string_view name) const;

  GLuint id_;
  Shader vertex_shader_, fragment_shader_;
  mutable std::unordered_map<std::string, GLint, StringViewHash, std::equal_to<>> uniform_locations_;
};

}  // namespace gfx

#endif  // GRAPHICS_SHADER_PROGRAM_H_
