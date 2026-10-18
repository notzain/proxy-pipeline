cmake_minimum_required(VERSION 4.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

find_package(libxml2 REQUIRED)
find_package(XercesC REQUIRED)

# Automatically create a target and binary for each discovered backend:
# - order_validation_handler_file
# - order_validation_handler_socket
proxy_add_handler_all_backends(order_validation_handler
    SOURCES ${SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    EXTRA_LIBS
        LibXml2::LibXml2
        XercesC::XercesC
)

