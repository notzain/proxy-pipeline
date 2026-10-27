cmake_minimum_required(VERSION 4.0)
project(proxylog LANGUAGES CXX)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(proxylog ${SOURCES})

target_include_directories(proxylog
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(TARGET spdlog::spdlog_header_only)
    target_link_libraries(proxylog PRIVATE spdlog::spdlog_header_only)
else()
    target_link_libraries(proxylog PRIVATE spdlog::spdlog)
endif()
target_link_libraries(proxylog PUBLIC fmt::fmt)

if(APPLE)
    target_compile_definitions(proxylog PUBLIC SPDLOG_NO_TLS)
endif()

target_compile_features(proxylog PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

if(NOT TARGET proxylog::proxylog)
  add_library(proxylog::proxylog ALIAS proxylog)
endif()

if(NOT TARGET Proxy::Log)
  add_library(Proxy::Log ALIAS proxylog)
endif()
