cmake_minimum_required(VERSION 4.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

# Automatically create target and binary for each discovered backend:
# - route_handler_file
# - route_handler_socket
# - route_handler_uds
# - route_handler_shm
proxy_add_handler_all_backends(route_handler
    SOURCES ${SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

