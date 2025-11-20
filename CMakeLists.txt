cmake_minimum_required(VERSION 3.22)
project(mesh_simplification VERSION 1.0.0 LANGUAGES CXX)

# Defining CMAKE_CXX_FLAGS and CMAKE_EXE_LINKER_FLAGS in CMake presets will overwrite default values provided by CMake.
# To prevent this, compile and link options for a preset are defined in cache/environment variables and applied here.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $ENV{CXX_FLAGS} $CACHE{CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} $CACHE{CXX_LINKER_FLAGS}")

add_library(glm_compile_definitions INTERFACE) # interface target for sharing common compiler definitions
target_compile_definitions(glm_compile_definitions INTERFACE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_XYZW_ONLY)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
