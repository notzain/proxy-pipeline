cmake_minimum_required(VERSION 4.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

# Automatically create a target and binary for each discovered backend:
# - udp_egress_handler_shm (linked with backend-shm)
# - udp_egress_handler_uds (linked with backend-uds)
# - udp_egress_handler_socket (linked with backend-socket)
# - udp_egress_handler_file (linked with backend-file)
proxy_add_handler_all_backends(udp_egress_handler
    SOURCES ${SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

