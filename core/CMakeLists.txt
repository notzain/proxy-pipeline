cmake_minimum_required(VERSION 4.0)
project(core LANGUAGES CXX)

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(pugixml REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(core ${SOURCES})

target_include_directories(core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(core
    PUBLIC
        backend-interface
        proxylog::proxylog
        fmt::fmt
        nlohmann_json::nlohmann_json
        pugixml::pugixml
)

target_compile_features(core PUBLIC cxx_std_20)

include(GNUInstallDirs)
install(TARGETS core
    EXPORT core-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
