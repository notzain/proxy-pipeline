cmake_minimum_required(VERSION 4.0)
project(backend-interface LANGUAGES CXX)

if(NOT TARGET backend-interface)
  file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  )

  add_library(backend-interface ${SOURCES})

  target_include_directories(backend-interface
      PUBLIC
          "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
          "$<INSTALL_INTERFACE:include>"
  )

  target_compile_features(backend-interface PUBLIC cxx_std_20)

  if(NOT TARGET backend-interface::backend-interface)
    add_library(backend-interface::backend-interface ALIAS backend-interface)
  endif()
endif()

