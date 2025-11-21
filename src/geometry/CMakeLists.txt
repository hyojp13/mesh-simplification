add_library(geometry STATIC)

target_sources(geometry PUBLIC FILE_SET HEADERS
                               BASE_DIRS ${SRC_DIR}
                               FILES face.h
                                     half_edge.h
                                     half_edge_mesh.h
                                     mesh_simplifier.h
                                     vertex.h
                        PRIVATE face.cpp
                                half_edge_mesh.cpp
                                mesh_simplifier.cpp)

find_package(glm CONFIG REQUIRED)
target_link_libraries(geometry PUBLIC glm::glm)
