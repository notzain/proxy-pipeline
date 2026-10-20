cmake_minimum_required(VERSION 4.0)
project(ProxyTests LANGUAGES CXX)

find_package(Catch2 3 REQUIRED)

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()

find_package(libxml2 REQUIRED)
find_package(XercesC REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(proxy_tests
    ${TEST_SOURCES}
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/src/XmlVerificationHandler.cpp"
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/src/XmlSchemaValidator.cpp"
    "${CMAKE_SOURCE_DIR}/handler/order_validation_handler/src/OrderValidationHandler.cpp"
    "${CMAKE_SOURCE_DIR}/handler/udp_ingress_handler/src/UdpIngressHandler.cpp"
)

target_include_directories(proxy_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/handler/xml_verification_handler/include"
    "${CMAKE_SOURCE_DIR}/handler/order_validation_handler/include"
    "${CMAKE_SOURCE_DIR}/handler/udp_ingress_handler/include"
)

target_link_libraries(proxy_tests PRIVATE
    core
    backend-socket
    backend-uds
    backend-shm
    proxylog::proxylog
    LibXml2::LibXml2
    XercesC::XercesC
    Catch2::Catch2WithMain
)

target_compile_features(proxy_tests PRIVATE cxx_std_20)

include(CTest)
include(Catch)
catch_discover_tests(proxy_tests)
