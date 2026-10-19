cmake_minimum_required(VERSION 4.0)
project(backend-socket LANGUAGES CXX)

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
    list(FILTER SOURCES EXCLUDE REGEX ".*/IoUringSocketPoller\\..*")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(FILTER SOURCES EXCLUDE REGEX ".*/KqueueSocketPoller\\..*")
    find_package(liburing REQUIRED)
else()
    list(FILTER SOURCES EXCLUDE REGEX ".*/(IoUring|Kqueue)SocketPoller\\..*")
endif()

add_library(backend-socket ${SOURCES})

target_include_directories(backend-socket
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-socket
    PUBLIC
        backend-interface::backend-interface
        proxylog::proxylog
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(backend-socket PRIVATE liburing::liburing)
endif()

target_compile_features(backend-socket PUBLIC cxx_std_20)

if(NOT TARGET backend-socket::backend-socket)
  add_library(backend-socket::backend-socket ALIAS backend-socket)
endif()
