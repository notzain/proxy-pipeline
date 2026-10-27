cmake_minimum_required(VERSION 4.0)
project(backend-shm LANGUAGES CXX)

if(NOT TARGET backend-interface::backend-interface)
  find_package(backend-interface REQUIRED)
endif()

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()

find_package(fmt REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(backend-shm ${SOURCES})

target_include_directories(backend-shm
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-shm
    PUBLIC
        backend-interface::backend-interface
        proxylog::proxylog
        fmt::fmt
)

target_compile_features(backend-shm PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

if(NOT TARGET backend-shm::backend-shm)
  add_library(backend-shm::backend-shm ALIAS backend-shm)
endif()

