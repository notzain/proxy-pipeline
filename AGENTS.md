#AGENTS.md

Guidelines and conventions for AI agents and developers working on this project.

---

## 1. Project Overview & Architecture

- **Language Standard**: C++20 (`set(CMAKE_CXX_STANDARD 20)` with `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`).
- **Build System**: Modern CMake 4.x (`cmake_minimum_required(VERSION 4.0)`).
- **Package Manager**: Conan 2.x integrated via CMake Dependency Provider (`conan_provider.cmake` from `cmake-conan` branch `develop2`).
- **Dependencies Manifest**: `conanfile.py` in the repository root.
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
- **Manifest**: Use `conanfile.py` at the root to declare packages and discover backends from the Conan dependency graph:
  ```python
  from conan import ConanFile
  from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout

  class ProxyConan(ConanFile):
      name = "proxy"
      version = "0.1.0"
      package_type = "application"
      settings = "os", "compiler", "build_type", "arch"
#...
  ```
- **CMakeLists.txt Agnosticism**: Never add Conan-specific logic inside target `CMakeLists.txt`. Standard modern CMake `find_package(<pkg> REQUIRED)` and `target_link_libraries(<target> PRIVATE <pkg>::<pkg>)` must be used. `conan_provider.cmake` intercepts `find_package` calls and invokes `conan install` automatically.
- **Conan Packaging of Subprojects**:
  - `log`, `backend/interface`, `backend/file`, `backend/socket`, and `core` each have a `conanfile.py` recipe.
  - To package them into the local Conan 2 cache:
    ```bash
    conan export log
    conan export backend/interface
    conan export backend/file
    conan export backend/socket
    conan export core
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
- **Tooling Constraint**: Never invoke `clangd` directly in background scripts or commands. Rely on compiler diagnostics and `clang-format` for code layout.
- **Coding Conventions & Control Flow**:
  - Prefer early returns, `break`, and `continue` statements over deep nesting to keep indentation flat, readable, and focused on the happy path.
  - After touching a header or source file, remove any unused includes from that file.
- **Git Workflow & Branching Policy**:
  - **Feature Branches**: All new features and non-trivial modifications must be developed inside dedicated feature branches (e.g. `feat/<feature-name>`) and merged back into the base branch (`master` / `main`) only when completed and verified. Never work directly on `master` for feature development.
  - **Explicit Approval Required**: Never create a git commit without the user's explicit prior approval. Always ask for permission and get confirmation before executing `git commit`.
  - When approved, create a **Conventional Commit** (`feat:`, `fix:`, `refactor:`, `chore:`, etc.).
  - Structure commit messages with a clear summary line, followed by bulleted details of the changes made.

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
  - `include/`: Public headers exposed to consumers of the library (e.g. `include/log/Log.hpp`, `include/core/handler/IHandler.hpp`, `include/backend/IBackend.hpp`, `include/backend/IConfig.hpp`).
  - `src/`: Implementation files (`.cpp`) and private/internal headers (`.hpp`).
- **Separation of Declarations & Implementations**:
  - All non-templated function and method implementations must reside in source files (`.cpp`). Headers (`.hpp`) are reserved for declarations only. Templated classes/functions are the only exception to this rule.
- **Single-Class-Per-File Policy (No Monolithic or Nested Large Classes)**:
  - Substantial or standalone classes (e.g. validator classes, format encoders/decoders, strategy engines, or complex helper types) must **never** be defined as in-line private implementations within a parent class's source file.
  - Each significant class must have its own dedicated pair of files (`<ClassName>.hpp` and `<ClassName>.cpp`). Internal or implementation-detail classes belong in `src/` (or private subdirectories) if not intended for public consumption.
- **Header Include Policy (Interface vs. Concrete Implementation)**:
  - Header files (`.hpp`) must include only abstract interfaces (e.g. `Backend::IBackend`, `Backend::IInputChannel`, `Backend::IOutputChannel`, `Core::Handler::IHandler`, `Core::Threading::IExecutionStrategy`).
  - Concrete class implementations (e.g. `FileInputChannel.hpp`, `SocketOutputChannel.hpp`, `DedicatedThreadStrategy.hpp`) must **never** be included in header files;
they must be included in source files(`.cpp`) only.
    - **Naming Conventions** : -**Interfaces** : Interface class names must start with a capital `I` prefix(e.g., `ILogSink`, `IHandler`, `IBackend`, `IConfig`).
    - **Namespaces** : Namespaces must always be* * PascalCase * *(e.g., `Backend`, `Core`, `Proxy`, `Proxy::Log`, `Proxy::Backend::File`).Note : formatting / layout is maintained by `clang - format`,
    while identifier casing is enforced via `.clang - tidy` (`readability - identifier - naming.NamespaceCase : CamelCase`).
    - **Alpha Development& No Backwards Compatibility** : -The project is in active alpha development.Do not maintain backwards compatibility shims,
    forwarding headers, deprecated type aliases, or transitional wrappers when refactoring or moving interfaces and types.Cleanly update call sites and references directly to the authoritative types and paths.

                                                                                              -- -

                                                                                              ##7. Documentation(Doxygen)

        - **Location Convention** : Doxygen documentation comments(`/** ... */`) must be placed in the** header file**(`.hpp`)alongside declarations.- **Source File Exception** : Place Doxygen comments in the source file(`.cpp`) only if the entity / function is strictly defined and used within that source file(e.g., file - static helpers or internal anonymous - namespace functions).

                                                                                                                                                                                                                                                          -- -

                                                                                                                                                                                                                                                          ##8. Architectural Layering& Abstraction

        - **Layer Responsibilities** : - `log` (`proxylog` target, `Proxy::Log` / `proxylog::proxylog`)
    : -Abstract universal logging library encapsulating the underlying logging engine(`spdlog`).
        - Exposes the abstract interface `ILogSink`:
      ```cpp class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void log(Level level, std::string_view message) = 0;
    virtual void flush() = 0;
    virtual void setLevel(Level level) = 0;
    [[nodiscard]] virtual Level getLevel() const = 0;
};
```
    - Pre - built sink implementations encapsulating `spdlog`: - `ConsoleLogSink`: Formatted output to stdout / stderr.
    - `FileLogSink`: File logging via `spdlog::sinks::basic_file_sink_mt` (with optional console mirroring).
    - `SocketLogSink`: UDP datagram output via `spdlog::sinks::udp_sink_mt` (with optional console mirroring).
    - Single initialization interface method :
      ```cpp
    void Proxy::Log::init(std::unique_ptr<Proxy::Log::ILogSink> sink);
```
    - Consumers include `"log/Log.hpp"` and call `Proxy::Log:: { trace, debug, info, warn, error, critical }` using string views or `fmt`-compatible formatting expressions.
  - `backend/interface` (`backend-interface` target):
    - `IConfig`: Abstract configuration interface providing strongly-typed templated getters (`get<T>(key)`, `getOr<T>(key, default)`).
    - `IInputChannel`: Abstract interface for receiving messages (`receive(timeout)`).
    - `IOutputChannel`: Abstract interface for transmitting messages (`send(span<byte>)`).
    - `IBackend`: Underlying transport interface configuring channels (`createInputChannel(name, config)`, `createOutputChannel(name, config)`), configured via `configure(const IConfig&)`. Declares `std::unique_ptr<IBackend> createBackend()`.
  - `backend/*`: Concrete transport implementations of `Backend::IBackend`:
    - Depend on `backend-interface` and `proxylog` (no dependency on `core`).
    - `backend-file`: Implements `FileBackend`, `FileInputChannel`, and `FileOutputChannel` watching/writing directory paths configured via `channel.<name>.path` or `file.path`. In `configure()`, initializes logging via `Proxy::Log::init(std::make_unique<Proxy::Log::FileLogSink>(logPath, logConsole))`.
    - `backend-socket`: Implements `SocketBackend`, `SocketInputChannel` (UDP listening/recvfrom), and `SocketOutputChannel` (UDP sending/sendto) configured via `channel.<name>.host`, `channel.<name>.port`, `channel.<name>.dest_host`, `channel.<name>.dest_port` or `socket.*`. In `configure()`, initializes logging via `Proxy::Log::init(std::make_unique<Proxy::Log::SocketLogSink>(logHost, logPort, logConsole))`.
    - Backends implement default constructors, `configure(const IConfig&)`, and define the factory function `Backend::createBackend()`. Backends never depend on specific handlers.
  - `core`: Defines foundational domain classes, execution strategies, queues, configuration, and serializers (depends on `backend-interface` and `proxylog`):
    - `Core::Handler` (`include/core/handler/`, `src/handler/`): `IHandler` message processing contract (`onMessageReceived`, `onBatchReceived`, `processBatch`, `step`) and channel binding (`registerInputChannel`, `registerOutputChannel`). Orchestrated and supervised by `HandlerRunner` (`include/core/handler/HandlerRunner.hpp`), which encapsulates configuration, live StatsD telemetry, concurrency execution models (`ExecutionModel` variant with `SequentialModel`, `DedicatedThreadModel`, `ThreadPoolModel`), non-blocking asynchronous running via `startAsync()` and `wait()`, and graceful signal handling (SIGINT/SIGTERM).
    - `Core::Threading` (`include/core/threading/`, `src/threading/`): Concurrency primitives and execution models (`IExecutionStrategy`, `SequentialStrategy`, `DedicatedThreadStrategy`, `ThreadPoolStrategy`, `MessageTask`).
    - `Core::Queue` (`include/core/queue/`): High-performance lockless Single-Producer Single-Consumer (`SpscQueue`) ring buffer.
    - `Core::Config` (`include/core/config/`, `src/config/`): Key-value configuration store loaded from file (`Configuration::fromFile(path)`), implementing `Backend::IConfig`.
    - `Core::Serialization` (`include/core/serialization/`): Strongly-typed, trait-based serialization system (`TypeTraits<T>`, `serialize<Format, T>`, `deserialize<Format, T>`) organized by format and intermediate representation:
      - `Core::Serialization::Ir` (`include/core/serialization/ir/`): Dynamically-typed intermediate representation value tree (`Value`, `Converter` / `toIntermediate`, `fromIntermediate`).
      - `Core::Serialization::Json` (`include/core/serialization/json/`, `src/serialization/json/`): JSON format policy (`JsonFormat`) and serializers (`JsonSerializer<T>`, `JsonDeserializer<T>`).
      - `Core::Serialization::Xml` (`include/core/serialization/xml/`, `src/serialization/xml/`): XML format policy (`XmlFormat`) and serializers (`XmlSerializer<T>`, `XmlDeserializer<T>`).
  - `handler/*`: Concrete handlers and application executables. All handlers cleanly inherit solely from `IHandler`. Handler source files **must not** contain backend `#ifdef`s or include any concrete backend headers.
    - `print_handler`: Implements `PrintHandler` printing received messages.
    - `dummy_send`: Implements `DummySendHandler` registering an output channel and sending configured text blobs with a delay.
    - `dummy_receive`: Implements `DummyReceiveHandler` registering an input channel and printing received messages.
    - `split_handler`: Implements `SplitHandler` receiving messages from an input channel and forwarding/broadcasting them to multiple output channels.
    - `merge_handler`: Implements `MergeHandler` receiving messages from multiple input channels and redirecting them to a single output channel.
    - `xml_verification_handler`: Implements `XmlVerificationHandler` validating incoming XML payloads against an XSD schema using `libxml2` and forwarding valid messages to an output channel while discarding rejected messages.
- **Multi-Backend Handler Generation & Conan Discovery (`ProxyBackends.cmake`)**:
  - The CMake module `cmake/ProxyBackends.cmake` provides:
    - `proxy_discover_backends()`: Discovers available backends directly through the Conan 2 dependency graph (`ConanBackends.cmake` generated by `conanfile.py`) instead of filesystem directory scanning. For in-tree backends, it adds the subdirectories automatically; for external backends, it links the Conan target.
    - `proxy_add_handler_all_backends(<handler_base_name> SOURCES ... [INCLUDE_DIRS ...])`: Generates executable targets for each discovered backend, named `<handler_base_name>_<backend_name>` (e.g. `print_handler_file` and `print_handler_socket`), outputting directly into the root `bin/` directory.
    - `proxy_export_targets_manifest(<output_file>)`: Generates `manifest.json` (and `targets.json`) tailored for GitLab CI matrix configuration, as well as `child-pipeline.yml` for dynamic child pipelines.
  - This eliminates hardcoded `STREQUAL` checks from individual handler CMake files and automatically builds every backend combination.

---

## 9. Containerization & OCI Distribution (Pattern 2)

- **Decoupled OCI Packaging**:
  - A single parameterized multi-stage [`Dockerfile`](file:///Users/zainahmad/Workspace/Proxy/Dockerfile) builds Linux binaries inside a `builder` stage and packages stripped binaries into lightweight distroless runtime images (`gcr.io/distroless/cc-debian12:nonroot`).
  - **Zero Maintenance on Adding Handlers**: CMake outputs `${CMAKE_BINARY_DIR}/manifest.json` and `${CMAKE_BINARY_DIR}/child-pipeline.yml`. Neither the `Dockerfile`, CI pipelines, nor local scripts require manual updates when new handlers or backends are created.
- **Local Testing**:
  - Run `./scripts/build-oci.sh` to compile and package all discovered handlers locally into OCI images (e.g. `proxy/print_handler_file:latest`).
  - Or package a single handler: `./scripts/build-oci.sh print_handler_socket`.
- **CI/CD Automation (GitLab CI)**:
  - Automated via [`.gitlab-ci.yml`](file:///Users/zainahmad/Workspace/Proxy/.gitlab-ci.yml) using dynamic child pipeline generation driven by `manifest.json` and `child-pipeline.yml`.

---

## 10. Handler Chain Orchestration (`scripts/chain_runner.py`)

- **Role**: Launches and coordinates arbitrary chains of handler executables across any configured backend using a single declarative configuration file (`.toml` or `.json`).
- **Configuration Layout (`chains/*.toml` or `chains/*.json`)**:
  - `backend`: The transport backend shared by the chain (`"file"`, `"socket"`, etc.).
  - `config`: Global / backend transport configuration (e.g. channel directory paths or UDP hosts and ports).
  - `handlers`: List of handlers declaring their `type` (e.g. `dummy_send`, `dummy_receive`, `print_handler`), unique instance `name`, channel bindings (`input_channel`, `output_channel`), and handler-specific `config` parameters.
- **Execution**:
  ```bash
  ./scripts/chain_runner.py chains/example_file_chain.toml
  # or with custom build binary path:
  ./scripts/chain_runner.py chains/example_socket_chain.toml --bin-dir ./build/gcc16-debug/bin
  ```
- **Lifecycle**: Automatically coordinates SIGINT / Ctrl+C across all active processes in the chain for clean, graceful shutdowns.

---

## 11. Feature Documentation & Task Tracking (`docs/`)

- **Rule**: Whenever adding new features or making substantial changes to an existing feature, create/update a dedicated markdown file in the `docs/` directory named after the feature (e.g., `docs/<feature-name>.md`).
- **Required Structure**:
  - **Overview**: High-level problem statement and objectives.
  - **Design Decisions**: Rationale and architectural agreements reached.
  - **Tasks / Checklist**: Interactive markdown checklist (`- [ ]` / `- [x]`) broken down into concrete actionable implementation steps that can be tracked and ticked off as work progresses.
