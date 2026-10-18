cmake_minimum_required(VERSION 4.0)

if(NOT TARGET backend-interface)
  add_library(backend-interface INTERFACE)

  target_include_directories(backend-interface
      INTERFACE
          "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
          "$<INSTALL_INTERFACE:include>"
  )

  target_compile_features(backend-interface INTERFACE cxx_std_20)
  add_library(backend-interface::backend-interface ALIAS backend-interface)
endif()
