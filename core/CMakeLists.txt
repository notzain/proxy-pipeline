cmake_minimum_required(VERSION 4.0)
project(core LANGUAGES CXX)

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)

set(CORE_EXTRA_LIBS)

if(PROXY_ENABLE_PUGIXML)
    find_package(pugixml REQUIRED)
    list(APPEND CORE_EXTRA_LIBS pugixml::pugixml)
endif()

if(PROXY_ENABLE_XERCES)
    find_package(XercesC REQUIRED)
    list(APPEND CORE_EXTRA_LIBS XercesC::XercesC)
endif()

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
        common
        proxylog::proxylog
        fmt::fmt
        nlohmann_json::nlohmann_json
        ${CORE_EXTRA_LIBS}
)

target_compile_features(core PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

include(GNUInstallDirs)
install(TARGETS core
    EXPORT core-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
