cmake_minimum_required(VERSION 4.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

set(ROUTE_HANDLER_EXTRA_LIBS)

if(PROXY_ENABLE_LUA)
    find_package(lua REQUIRED)
    find_package(sol2 REQUIRED)
    list(APPEND ROUTE_HANDLER_EXTRA_LIBS sol2::sol2 lua::lua)
endif()

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
    EXTRA_LIBS
        ${ROUTE_HANDLER_EXTRA_LIBS}
)

