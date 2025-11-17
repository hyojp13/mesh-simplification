#ifndef APP_H_
#define APP_H_

#include "graphics/window.h"

namespace app {

void Run(const char* app_name, gfx::Window::Size window_size, gfx::OpenGlVersion opengl_version);

}

#endif  // APP_H_
