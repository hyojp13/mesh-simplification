#include "graphics/window.h"

#include <cassert>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace gfx {

namespace {

class GlfwContext {
public:
  static void Init(const OpenGlVersion opengl_version) {
    [[maybe_unused]] static const GlfwContext kInstance{opengl_version};
  }

  GlfwContext(const GlfwContext&) = delete;
  GlfwContext(GlfwContext&&) noexcept = delete;

  GlfwContext& operator=(const GlfwContext&) = delete;
  GlfwContext& operator=(GlfwContext&&) noexcept = delete;

  ~GlfwContext() noexcept { glfwTerminate(); }

private:
  explicit GlfwContext(const OpenGlVersion opengl_version) {
#ifndef NDEBUG
    glfwSetErrorCallback([](const int error_code, const char* const description) {
      std::cerr << std::format("GLFW error {}: {}", error_code, description);
    });
#endif
    if (glfwInit() == GLFW_FALSE) throw std::runtime_error{"GLFW initialization failed"};

    const auto [major_version, minor_version] = opengl_version;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_version);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_version);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwSwapInterval(1);
  }
};

class Gl3wContext {
public:
  static void Init(const OpenGlVersion opengl_version) {
    [[maybe_unused]] static const Gl3wContext kInstance{opengl_version};
  }

  Gl3wContext(const Gl3wContext&) = delete;
  Gl3wContext(Gl3wContext&&) noexcept = delete;

  Gl3wContext& operator=(const Gl3wContext&) = delete;
  Gl3wContext& operator=(Gl3wContext&&) noexcept = delete;

  ~Gl3wContext() noexcept = default;

private:
  static void HandleDebugMessageReceived(const GLenum source,
                                         const GLenum type,
                                         const GLuint id,
                                         const GLenum severity,
                                         const GLsizei /*length*/,
                                         const GLchar* const message,
                                         const void* /*user_param*/) {
    std::string message_source;
    switch (source) {
        // clang-format off
    case GL_DEBUG_SOURCE_API: message_source = "API"; break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: message_source = "WINDOW SYSTEM"; break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: message_source = "SHADER COMPILER"; break;
    case GL_DEBUG_SOURCE_THIRD_PARTY: message_source = "THIRD PARTY"; break;
    case GL_DEBUG_SOURCE_APPLICATION: message_source = "APPLICATION"; break;
    default: message_source = "OTHER"; break;
        // clang-format on
    }

    std::string message_type;
    switch (type) {
        // clang-format off
    case GL_DEBUG_TYPE_ERROR: message_type = "ERROR"; break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: message_type = "DEPRECATED BEHAVIOR"; break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: message_type = "UNDEFINED BEHAVIOR"; break;
    case GL_DEBUG_TYPE_PORTABILITY: message_type = "PORTABILITY"; break;
    case GL_DEBUG_TYPE_PERFORMANCE: message_type = "PERFORMANCE"; break;
    default: message_type = "OTHER"; break;
        // clang-format on
    }

    std::string message_severity;
    switch (severity) {
        // clang-format off
    case GL_DEBUG_SEVERITY_HIGH: message_severity = "HIGH"; break;
    case GL_DEBUG_SEVERITY_MEDIUM: message_severity = "MEDIUM"; break;
    case GL_DEBUG_SEVERITY_LOW: message_severity = "LOW"; break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: message_severity = "NOTIFICATION"; break;
    default: message_severity = "OTHER"; break;
        // clang-format on
    }

    std::clog << format("OpenGL Debug ({}): Source: {}, Type: {}, Severity: {}\n{}\n",
                        id,
                        message_source,
                        message_type,
                        message_severity,
                        message);
  }

  explicit Gl3wContext(const OpenGlVersion opengl_version) {
    if (gl3wInit() != GL3W_OK) {
      throw std::runtime_error{"OpenGL initialization failed"};
    }

    if (const auto [major_version, minor_version] = opengl_version;
        gl3wIsSupported(major_version, minor_version) == 0) {
      throw std::runtime_error{std::format("OpenGL {}.{} not supported", major_version, minor_version)};
    }

#ifndef NDEBUG
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    std::clog << std::format("OpenGL version: {}, GLSL version: {}\n",
                             reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                             reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(HandleDebugMessageReceived, nullptr);
#endif

    // configure OpenGL graphics pipeline state
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
  }
};

using UniqueGlfwWindow = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>;

UniqueGlfwWindow CreateGlfwWindow(const char* const title,
                                  const Window::Size window_size,
                                  const OpenGlVersion opengl_version) {
  GlfwContext::Init(opengl_version);

  const auto& [width, height] = window_size;
  UniqueGlfwWindow glfw_window{glfwCreateWindow(width, height, title, nullptr, nullptr), glfwDestroyWindow};
  if (glfw_window == nullptr) throw std::runtime_error{"Failed to create GLFW window"};

  return glfw_window;
}

}  // namespace

Window::Window(const char* const title, const Size size, const OpenGlVersion opengl_version)
    : window_{CreateGlfwWindow(title, size, opengl_version)} {
  glfwSetWindowUserPointer(window_.get(), this);
  glfwMakeContextCurrent(window_.get());

  glfwSetKeyCallback(
      window_.get(),
      [](GLFWwindow* const window, const int key, const int /*scancode*/, const int action, const int /*modifiers*/) {
        if (const auto* const self = static_cast<Window*>(glfwGetWindowUserPointer(window)); self->key_event_handler_) {
          self->key_event_handler_(key, action);
        }
      });

  glfwSetCursorPosCallback(window_.get(), [](GLFWwindow* const window, const double x, const double y) {
    if (const auto* const self = static_cast<Window*>(glfwGetWindowUserPointer(window)); self->cursor_event_handler_) {
      self->cursor_event_handler_(static_cast<float>(x), static_cast<float>(y));
    }
  });

  glfwSetScrollCallback(window_.get(), [](GLFWwindow* const window, const double x, const double y) {
    if (const auto* const self = static_cast<Window*>(glfwGetWindowUserPointer(window)); self->scroll_event_handler_) {
      self->scroll_event_handler_(static_cast<float>(x), static_cast<float>(y));
    }
  });

  glfwSetWindowSizeCallback(window_.get(), [](GLFWwindow* const window, const int width, const int height) {
    if (const auto* const self = static_cast<Window*>(glfwGetWindowUserPointer(window)); self->resize_event_handler_) {
      self->resize_event_handler_(Size{.width = width, .height = height});
    }
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    glViewport(0, 0, framebuffer_width, framebuffer_height);
  });

  Gl3wContext::Init(opengl_version);
}

}  // namespace gfx
