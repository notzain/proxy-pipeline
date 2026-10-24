find_package(fmt REQUIRED)

file(GLOB_RECURSE TOOLS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(generate_sample_pcap ${TOOLS_SOURCES})
target_link_libraries(generate_sample_pcap PRIVATE core fmt::fmt)
