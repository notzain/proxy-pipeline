cmake_minimum_required(VERSION 4.0)
project(ProxyBenchmark LANGUAGES CXX)

find_package(libxml2 REQUIRED)
find_package(fmt REQUIRED)

file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_executable(proxy_benchmark
    ${BENCH_SOURCES}
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/src/XmlVerificationHandler.cpp"
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/src/XmlSchemaValidator.cpp"
)

target_include_directories(proxy_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/include"
)

target_link_libraries(proxy_benchmark PRIVATE
    core
    backend-socket
    backend-uds
    proxylog::proxylog
    LibXml2::LibXml2
    fmt::fmt
)

target_compile_features(proxy_benchmark PRIVATE cxx_std_20)

