cmake_minimum_required(VERSION 4.0)
project(ProxyTests LANGUAGES CXX)

find_package(Catch2 3 REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(proxy_tests ${TEST_SOURCES})

target_link_libraries(proxy_tests PRIVATE
    core
    Catch2::Catch2WithMain
)

target_compile_features(proxy_tests PRIVATE cxx_std_20)

include(CTest)
include(Catch)
catch_discover_tests(proxy_tests)
