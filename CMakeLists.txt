cmake_minimum_required(VERSION 4.0)
project(Proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Export compile commands for language servers and tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure all executable binaries end up in a root bin folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Add cmake/ module path and include ProxyBackends module
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProxyBackends)

# Backend interface library
add_subdirectory(backend/interface)

# Core domain library (depends on backend-interface)
add_subdirectory(core)

# Automatically discover all backends under backend/
proxy_discover_backends("${CMAKE_CURRENT_SOURCE_DIR}/backend")

# Handlers (will combine across all discovered backends)
add_subdirectory(handler/print_handler)

# Tests
enable_testing()
add_subdirectory(test)

# Export manifest of all generated handler targets for CI/Docker packaging
proxy_export_targets_manifest("${CMAKE_BINARY_DIR}/manifest.json")
