#include <cstdlib>
#include <exception>
#include <iostream>

#include "app.h"  // NOLINT(build/include_subdir)

int main() {  // NOLINT(bugprone-exception-escape): exceptions are not enabled for standard error stream
  try {
    static constexpr gfx::Window::Size kWindowSize{.width = 1920, .height = 1080};
    static constexpr gfx::OpenGlVersion kOpenGlVersion{.major = 4, .minor = 1};
    app::Run("Mesh Simplification", kWindowSize, kOpenGlVersion);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  } catch (...) {
    std::cerr << "An unknown error occurred\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
