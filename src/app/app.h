#ifndef APP_APP_H_
#define APP_APP_H_

#include "graphics/window.h"

namespace gfx {
struct OpenGlVersion;
}

namespace app {

/**
 * @brief Initializes and runs the application.
 * @details This program implements a simple OpenGL model viewer which demonstrates mesh simplification by pressing the
 *          'S' key. It also provides orbit camera controls to view the mesh using the mouse buttons and scroll wheel.
 * @param app_name The UTF-8 application name.
 * @param window_size The window width and height.
 * @param opengl_version The OpenGL version to use for the application.
 */
void Run(const char* app_name, gfx::Window::Size window_size, gfx::OpenGlVersion opengl_version);

}  // namespace app

#endif  // APP_APP_H_
