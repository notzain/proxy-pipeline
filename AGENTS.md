# AGENTS.md

Guidelines and conventions for AI agents and developers working on this project.

---

## 1. Project Overview & Architecture

- **Language Standard**: C++20 (`set(CMAKE_CXX_STANDARD 20)` with `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`).
- **Build System**: Modern CMake 4.x (`cmake_minimum_required(VERSION 4.0)`).
- **Package Manager**: Conan 2.x integrated via CMake Dependency Provider (`conan_provider.cmake` from `cmake-conan` branch `develop2`).
- **Dependencies Manifest**: `conanfile.txt` in the repository root.
- **Root CMake Role**: The root `CMakeLists.txt` does not compile binaries or libraries directly. Targets are organized and declared inside subdirectories (e.g. `src/`, `apps/`, `libs/`, `tests/`) and added via `add_subdirectory(...)`.

---

## 2. Source File Management (Globbing Rule)

- **Rule**: CMake subprojects collect their source and header files using **file globbing** rather than manually listing individual source files.
- **Developer Overhead**: This policy is established to eliminate developer overhead when adding, renaming, or removing source files.
- **CMake Implementation Pattern**:
  When creating a subproject `CMakeLists.txt`, always use `file(GLOB_RECURSE ... CONFIGURE_DEPENDS ...)` so CMake re-runs configuration automatically when files are added or deleted:
  ```cmake
  file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  )
  add_library(my_subproject ${SOURCES})
  target_include_directories(my_subproject PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  ```

---

## 3. Dependency Management (Conan 2 + cmake-conan)

- **Provider Location**: `cmake/conan_provider.cmake` acts as CMake's top-level dependency provider.
- **Manifest**: Use `conanfile.txt` at the root to declare packages:
  ```ini
  [requires]
  # example: fmt/11.0.2

  [generators]
  CMakeDeps
  CMakeToolchain

  [layout]
  cmake_layout
  ```
- **CMakeLists.txt Agnosticism**: Never add Conan-specific logic inside target `CMakeLists.txt`. Standard modern CMake `find_package(<pkg> REQUIRED)` and `target_link_libraries(<target> PRIVATE <pkg>::<pkg>)` must be used. `conan_provider.cmake` intercepts `find_package` calls and invokes `conan install` automatically.
- **Conan Packaging of Subprojects**:
  - `core`, `backend/file`, and `backend/socket` each have a `conanfile.py` recipe.
  - To package them into the local Conan 2 cache:
    ```bash
    conan create core --build=missing -s build_type=Debug
    conan create backend/file --build=missing -s build_type=Debug
    conan create backend/socket --build=missing -s build_type=Debug
    ```

---

## 4. Code Formatting & Style

- **Formatter**: `clang-format`.
- **Base Style**: **WebKit** (`.clang-format` located at project root).
- **Enforcement**:
  Format files before committing or submitting changes:
  ```bash
  clang-format -i path/to/file.cpp path/to/file.hpp
  ```
  Or format all tracked source files:
  ```bash
  git ls-files '*.cpp' '*.hpp' '*.cc' '*.h' | xargs clang-format -i
  ```

---

## 5. Build & Configuration Commands

### Using CMake Presets (Recommended):
- Configure Debug:
  ```bash
  cmake --preset debug
  ```
- Configure Release:
  ```bash
  cmake --preset release
  ```
- Build:
  ```bash
  cmake --build --preset debug
  # or
  cmake --build --preset release
  ```

### Manual Configuration (Without Presets):
```bash
cmake -B build -S . -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

---

## 6. Project Structure & Naming Conventions

- **Directory Layout for Subprojects**:
  - `include/`: Public headers exposed to consumers of the library (e.g. `include/core/IHandler.hpp`).
  - `src/`: Implementation files (`.cpp`) and private/internal headers (`.hpp`).
- **Separation of Declarations & Implementations**:
  - All non-templated function and method implementations must reside in source files (`.cpp`). Headers (`.hpp`) are reserved for declarations only. Templated classes/functions are the only exception to this rule.
- **Naming Conventions**:
  - **Interfaces**: Interface class names must start with a capital `I` prefix (e.g., `IHandler`).
  - **Namespaces**: Namespaces must always be **PascalCase** (e.g., `Core`, `Proxy`, `Proxy::Backend::File`). Note: formatting/layout is maintained by `clang-format`, while identifier casing is enforced via `.clang-tidy` (`readability-identifier-naming.NamespaceCase: CamelCase`).

---

## 7. Documentation (Doxygen)

- **Location Convention**: Doxygen documentation comments (`/** ... */`) must be placed in the **header file** (`.hpp`) alongside declarations.
- **Source File Exception**: Place Doxygen comments in the source file (`.cpp`) only if the entity/function is strictly defined and used within that source file (e.g., file-static helpers or internal anonymous-namespace functions).

---

## 8. Architectural Layering & Abstraction

- **Layer Responsibilities**:
  - `core`: Defines foundational domain interfaces:
    - `IHandler`: Message processing lifecycle and polling event loop (`onMessageReceived(buffer)`, `nextMessage(timeout)`, `start()`, `stop()`).
    - `IBackend`: Underlying transport interface for message retrieval (`nextMessage(timeout)`), configured via `configure(const Configuration&)`. Declares `std::unique_ptr<IBackend> createBackend()`.
    - `Configuration`: Key-value configuration store loaded from file (`Configuration::fromFile(path)`). Applications must not set their own properties in code; properties always originate from configuration files.
  - `backend/*`: Concrete transport implementations of `IBackend`:
    - `backend-file`: Implements `FileBackend` watching a directory specified by `"file.path"`. Newly detected files are read and passed as messages, then deleted immediately.
    - `backend-socket`: Implements `SocketBackend` listening on a UDP socket specified by `"socket.host"` and `"socket.port"`, emitting received datagram payloads as messages (receive-only).
    - Backends implement default constructors, `configure(config)`, and define the factory function `Core::createBackend()`. Backends never depend on specific handlers.
  - `handler/*`: Concrete handlers and application executables. Handlers inherit from `IHandler` and accept `std::unique_ptr<IBackend>`. Handler source files **must not** contain backend `#ifdef`s or include any backend headers.
- **Multi-Backend Handler Generation & Auto-Discovery (`ProxyBackends.cmake`)**:
  - The CMake module `cmake/ProxyBackends.cmake` provides:
    - `proxy_discover_backends(<dir>)`: Automatically scans and registers all available backend subprojects under `backend/`.
    - `proxy_add_handler_all_backends(<handler_base_name> SOURCES ... [INCLUDE_DIRS ...])`: Generates executable targets for each discovered backend, named `<handler_base_name>_<backend_name>` (e.g. `print_handler_file` and `print_handler_socket`), outputting directly into the root `bin/` directory.
  - This eliminates hardcoded `STREQUAL` checks from individual handler CMake files and automatically builds every backend combination.
