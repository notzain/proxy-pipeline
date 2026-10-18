cmake_minimum_required(VERSION 4.0)

find_package(libxml2 REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

# Automatically create a target and binary for each discovered backend:
# - xml_verification_handler_file
# - xml_verification_handler_socket
proxy_add_handler_all_backends(xml_verification_handler
    SOURCES ${SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    EXTRA_LIBS
        LibXml2::LibXml2
)

