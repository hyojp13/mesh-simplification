add_library(graphics STATIC)

target_sources(graphics PUBLIC FILE_SET HEADERS
                               BASE_DIRS ${SRC_DIR}
                               FILES arc_camera.h
                                     material.h
                                     mesh.h
                                     obj_loader.h
                                     scene.h
                                     shader_program.h
                                     window.h
                        PRIVATE arc_camera.cpp
                                mesh.cpp
                                obj_loader.cpp
                                scene.cpp
                                shader_program.cpp
                                window.cpp)

find_package(OpenGL REQUIRED)
find_package(gl3w CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(graphics PUBLIC OpenGL::GL
                                      glfw
                                      glm::glm
                                      unofficial::gl3w::gl3w)

target_compile_definitions(graphics PUBLIC GLM_ENABLE_EXPERIMENTAL)
