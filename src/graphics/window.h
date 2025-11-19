#ifndef GRAPHICS_WINDOW_H_
#define GRAPHICS_WINDOW_H_

#include <concepts>
#include <functional>
#include <memory>
#include <utility>

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

namespace gfx {

/** @brief The application OpenGL version. */
struct OpenGlVersion {
  /** @brief The OpenGL major version. */
  int major = 0;

  /** @brief The OpenGL minor version. */
  int minor = 0;
};

/** @brief An abstraction for a GLFW window. */
class Window {
public:
  /** @brief The 2D window size. */
  struct Size {
    /** @brief The window width. */
    int width = 0;

    /** @brief The window height. */
    int height = 0;
  };

  /**
   * @brief Creates a window.
   * @param title The UTF-8 encoded window title.
   * @param size The 2D window size.
   * @param opengl_version The application OpenGL version.
   */
  Window(const char* title, Size size, OpenGlVersion opengl_version);

  /**
   * @brief Sets the key event handler.
   * @tparam Fn The callable key event handler that accepts a GLFW key (e.g, GLFW_KEY_ESCAPE) and action (e.g.,
   *            GLFW_PRESS) as arguments.
   * @param key_event_handler The event handler to be invoked when a key event is registered.
   */
  template <std::invocable<int, int> Fn>
  void OnKeyEvent(Fn&& key_event_handler) noexcept {
    key_event_handler_ = std::forward<Fn>(key_event_handler);
  }

  /**
   * @brief Sets the cursor move event handler.
   * @tparam Fn The callable cursor move event handler that accepts the cursor x and y positions as arguments.
   * @param cursor_event_handler The event handler to be invoked when a cursor move event is registered.
   */
  template <std::invocable<float, float> Fn>
  void OnCursorEvent(Fn&& cursor_event_handler) noexcept {
    cursor_event_handler_ = std::forward<Fn>(cursor_event_handler);
  }

  /**
   * @brief Sets the scroll event handler.
   * @tparam Fn The callable scroll event handler that accepts the scroll x and y offsets as arguments.
   * @param scroll_event_handler The event handler to be invoked when a scroll event is registered.
   */
  template <std::invocable<float, float> Fn>
  void OnScrollEvent(Fn&& scroll_event_handler) noexcept {
    scroll_event_handler_ = std::forward<Fn>(scroll_event_handler);
  }

  /**
   * @brief Sets the window resize event handler.
   * @tparam Fn The callable window resize event handler that accepts the new window size as an argument.
   * @param resize_event_handler The event handler to be invoked when a window resize event is registered.
   */
  template <std::invocable<Size> Fn>
  void OnResizeEvent(Fn&& resize_event_handler) noexcept {
    resize_event_handler_ = std::forward<Fn>(resize_event_handler);
  }

  /**
   * @brief Checks if a mouse button is currently pressed.
   * @param mouse_button The GLFW mouse button (e.g., GLFW_MOUSE_BUTTON_LEFT) to check.
   * @return @c true if @ref mouse_button is pressed, otherwise @c false.
   */
  [[nodiscard]] bool IsMouseButtonPressed(const int mouse_button) const noexcept {
    return glfwGetMouseButton(window_.get(), mouse_button) == GLFW_PRESS;
  }

  /**
   * @brief Gets the current window size.
   * @return The 2D window size.
   */
  [[nodiscard]] Size GetSize() const noexcept {
    auto width = 0, height = 0;
    glfwGetWindowSize(window_.get(), &width, &height);
    return Size{.width = width, .height = height};
  }

  /**
   * @brief Checks if the window close flag has been set.
   * @return @c true if the window close flag has been set, otherwise @c false.
   */
  [[nodiscard]] bool IsClosed() const noexcept { return glfwWindowShouldClose(window_.get()) == GLFW_TRUE; }

  /** @brief Sets the window close flag. */
  void Close() noexcept { glfwSetWindowShouldClose(window_.get(), GLFW_TRUE); }

  /** @brief Polls for window events. */
  static void PollEvents() noexcept { glfwPollEvents(); }

  /** @brief Swaps the front and back framebuffers. */
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
