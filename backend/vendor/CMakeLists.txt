cmake_minimum_required(VERSION 4.0)
project(backend-vendor LANGUAGES CXX)

if(NOT TARGET backend-interface::backend-interface)
  find_package(backend-interface REQUIRED)
endif()

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(backend-vendor ${SOURCES})

target_include_directories(backend-vendor
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-vendor
    PUBLIC
        backend-interface::backend-interface
        proxylog::proxylog
)

target_compile_features(backend-vendor PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

if(NOT TARGET backend-vendor::backend-vendor)
  add_library(backend-vendor::backend-vendor ALIAS backend-vendor)
endif()

