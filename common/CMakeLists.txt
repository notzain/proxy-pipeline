cmake_minimum_required(VERSION 4.0)
project(common LANGUAGES CXX)

if(NOT TARGET common)
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    )

    add_library(common ${SOURCES})

    target_include_directories(common
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:include>"
    )

    target_compile_features(common PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

    if(NOT TARGET common::common)
        add_library(common::common ALIAS common)
    endif()
endif()

