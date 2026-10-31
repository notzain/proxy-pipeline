cmake_minimum_required(VERSION 4.0)
project(common LANGUAGES CXX)

if(NOT TARGET common)
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    )

    if(APPLE)
        list(FILTER SOURCES EXCLUDE REGEX ".*/IoUringSocketPoller\\..*")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(FILTER SOURCES EXCLUDE REGEX ".*/KqueueSocketPoller\\..*")
        if(PROXY_ENABLE_LIBURING)
            find_package(liburing REQUIRED)
        else()
            list(FILTER SOURCES EXCLUDE REGEX ".*/IoUringSocketPoller\\..*")
        endif()
    else()
        list(FILTER SOURCES EXCLUDE REGEX ".*/(IoUring|Kqueue)SocketPoller\\..*")
    endif()

    add_library(common ${SOURCES})

    target_include_directories(common
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:include>"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PROXY_ENABLE_LIBURING)
        target_link_libraries(common PRIVATE liburing::liburing)
        target_compile_definitions(common PRIVATE PROXY_HAS_LIBURING=1)
    endif()

    target_compile_features(common PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

    if(NOT TARGET common::common)
        add_library(common::common ALIAS common)
    endif()
endif()
