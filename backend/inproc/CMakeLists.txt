cmake_minimum_required(VERSION 4.0)
project(backend-inproc LANGUAGES CXX)

if(NOT TARGET backend-interface::backend-interface)
  find_package(backend-interface REQUIRED)
endif()

if(NOT TARGET common)
  find_package(common REQUIRED)
endif()

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(backend-inproc ${SOURCES})

target_include_directories(backend-inproc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-inproc
    PUBLIC
        backend-interface::backend-interface
        common
        proxylog::proxylog
)

target_compile_features(backend-inproc PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

if(NOT TARGET backend-inproc::backend-inproc)
  add_library(backend-inproc::backend-inproc ALIAS backend-inproc)
endif()

