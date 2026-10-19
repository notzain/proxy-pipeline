cmake_minimum_required(VERSION 4.0)
project(backend-uds LANGUAGES CXX)

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

if(APPLE)
    list(FILTER SOURCES EXCLUDE REGEX ".*/IoUringUdsPoller\\..*")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER SOURCES EXCLUDE REGEX ".*/KqueueUdsPoller\\..*")
    find_package(liburing REQUIRED)
else()
    list(FILTER SOURCES EXCLUDE REGEX ".*/(IoUring|Kqueue)UdsPoller\\..*")
endif()

add_library(backend-uds ${SOURCES})

target_include_directories(backend-uds
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-uds
    PUBLIC
        backend-interface::backend-interface
        proxylog::proxylog
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(backend-uds PRIVATE liburing::liburing)
endif()

target_compile_features(backend-uds PUBLIC cxx_std_20)

if(NOT TARGET backend-uds::backend-uds)
  add_library(backend-uds::backend-uds ALIAS backend-uds)
endif()
