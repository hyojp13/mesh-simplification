add_executable(mesh_simplification)

target_sources(mesh_simplification PRIVATE FILE_SET HEADERS
                                           BASE_DIRS ${SRC_DIR}
                                           FILES app.h
                                   PRIVATE app.cpp
                                           main.cpp)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(mesh_simplification PRIVATE geometry
                                                  graphics
                                                  glfw
                                                  glm::glm)

# Copy assets to the current binary directory so they're available at runtime
add_custom_target(copy_assets COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets
                                                                         ${CMAKE_CURRENT_BINARY_DIR}/assets)
add_dependencies(mesh_simplification copy_assets)
