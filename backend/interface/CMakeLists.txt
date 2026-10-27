cmake_minimum_required(VERSION 4.0)
project(backend-interface LANGUAGES CXX)

if(NOT TARGET common)
    find_package(common REQUIRED)
endif()

if(NOT TARGET backend-interface)
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

  add_library(backend-interface ${SOURCES})

  target_include_directories(backend-interface
      PUBLIC
          "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
          "$<INSTALL_INTERFACE:include>"
      PRIVATE
          "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )

  target_link_libraries(backend-interface PUBLIC common::common)

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_libraries(backend-interface PRIVATE liburing::liburing)
  endif()

  target_compile_features(backend-interface PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

  if(NOT TARGET backend-interface::backend-interface)
    add_library(backend-interface::backend-interface ALIAS backend-interface)
  endif()
endif()

