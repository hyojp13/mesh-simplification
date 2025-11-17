#ifndef GRAPHICS_WINDOW_H_
#define GRAPHICS_WINDOW_H_

#include <concepts>
#include <functional>
#include <memory>
#include <utility>

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

namespace gfx {

struct OpenGlVersion {
  int major = 0;
  int minor = 0;
};

class Window {
public:
  struct Size {
    int width = 0;
    int height = 0;
  };

  Window(const char* title, Size size, OpenGlVersion opengl_version);

  template <std::invocable<int, int> Fn>
  void OnKeyEvent(Fn&& key_event_handler) noexcept {
    key_event_handler_ = std::forward<Fn>(key_event_handler);
  }

  template <std::invocable<float, float> Fn>
  void OnCursorEvent(Fn&& cursor_event_handler) noexcept {
    cursor_event_handler_ = std::forward<Fn>(cursor_event_handler);
  }

  template <std::invocable<float, float> Fn>
  void OnScrollEvent(Fn&& scroll_event_handler) noexcept {
    scroll_event_handler_ = std::forward<Fn>(scroll_event_handler);
  }

  template <std::invocable<Size> Fn>
  void OnResizeEvent(Fn&& resize_event_handler) noexcept {
    resize_event_handler_ = std::forward<Fn>(resize_event_handler);
  }

  [[nodiscard]] bool IsMouseButtonPressed(const int mouse_button) const noexcept {
    return glfwGetMouseButton(window_.get(), mouse_button) == GLFW_PRESS;
  }

  [[nodiscard]] Size GetSize() const noexcept {
    auto width = 0, height = 0;
    glfwGetWindowSize(window_.get(), &width, &height);
    return Size{.width = width, .height = height};
  }

  [[nodiscard]] float GetAspectRatio() const noexcept {
    const auto [width, height] = GetSize();
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 0;
  }

  void Close() noexcept { glfwSetWindowShouldClose(window_.get(), GLFW_TRUE); }
  [[nodiscard]] bool IsClosed() const noexcept { return glfwWindowShouldClose(window_.get()) == GLFW_TRUE; }

  static void PollEvents() noexcept { glfwPollEvents(); }
  void SwapBuffers() noexcept { glfwSwapBuffers(window_.get()); }

private:
  std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window_;
  std::function<void(int, int)> key_event_handler_;
  std::function<void(float, float)> cursor_event_handler_;
  std::function<void(float, float)> scroll_event_handler_;
  std::function<void(Size)> resize_event_handler_;
};

}  // namespace gfx

#endif  // GRAPHICS_WINDOW_H_
