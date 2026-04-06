set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mesh_simplification_lib STATIC
            geometry/face.cpp
            geometry/half_edge_mesh.cpp
            geometry/mesh_simplifier.cpp
            mesh.cpp
            obj_io.cpp)

target_include_directories(mesh_simplification_lib PUBLIC ${SRC_DIR})

target_sources(mesh_simplification_lib PUBLIC FILE_SET HEADERS
               BASE_DIRS ${SRC_DIR}
               FILES math3d.h
                     mesh.h
                     obj_io.h
                     geometry/face.h
                     geometry/half_edge.h
                     geometry/half_edge_mesh.h
                     geometry/mesh_simplifier.h
                     geometry/vertex.h)

add_executable(mesh_simplification main.cpp)
target_link_libraries(mesh_simplification PRIVATE mesh_simplification_lib)
