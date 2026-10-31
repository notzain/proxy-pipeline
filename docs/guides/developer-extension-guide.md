# Developer Guide: Extending Handlers & Transport Backends

## 1. Overview & Objectives
This guide walks developers through creating new message handlers, registering channels, writing Catch2 unit tests, configuring declarative pipeline chains, and implementing new transport backends.

The framework adheres to strict design principles:
- **Transport Agnostic Business Logic**: Handlers never include transport headers (e.g. `SocketInputChannel.hpp`) or contain transport `#ifdef`s.
- **Zero File-Listing Overhead in CMake**: Handlers use CMake file globbing (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`). Adding new source files does not require editing `CMakeLists.txt`.
- **Compile-Time Polyfills**: Use `Common::Compat::` for C++20 standard types (`span`, `jthread`, etc.). Never inject into `namespace std`.

---

## 2. Creating a Custom Message Handler

Every handler inherits from `Proxy::Core::Handler::IHandler` (`core/include/core/handler/IHandler.hpp`).

### Step 1: Create the Subproject Directory Structure
Create a directory under `handler/` (e.g. `handler/filter_handler/`):

```
handler/filter_handler/
├── CMakeLists.txt
├── include/
│   └── FilterHandler.hpp
└── src/
    └── FilterHandler.cpp
```

### Step 2: Write `CMakeLists.txt`
All handlers use the `proxy_add_handler_all_backends` helper function:

```cmake
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

proxy_add_handler_all_backends(filter_handler
    SOURCES ${SOURCES}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
```
*Note: This automatically generates `filter_handler_shm`, `filter_handler_socket`, `filter_handler_uds`, `filter_handler_inproc`, etc., in the build directory.*

### Step 3: Define the Class Header (`include/FilterHandler.hpp`)
Headers must expose only abstract interfaces and never include concrete transport implementations:

```cpp
#pragma once

#include "core/handler/IHandler.hpp"
#include <string>

namespace Backend {
class IOutputChannel;
}

namespace Proxy::Handler {

class FilterHandler final : public Core::Handler::IHandler {
public:
    FilterHandler() = default;
    ~FilterHandler() override = default;

protected:
    bool onConfigure(const Core::Config::Configuration& config) override;
    Backend::MessageResult onMessageReceived(
        std::string_view channelName,
        const Backend::MessageView& message) override;

private:
    std::string m_forbiddenWord;
    Backend::IOutputChannel* m_outputChannel { nullptr };
};

} // namespace Proxy::Handler
```

### Step 4: Implement the Handler (`src/FilterHandler.cpp`)
Channel creation uses the backend injected into the base class (`backend()`):

```cpp
#include "FilterHandler.hpp"
#include "backend/core/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "backend/message/MessageResult.hpp"
#include "backend/message/MessageView.hpp"
#include "log/Log.hpp"
#include <algorithm>

namespace Proxy::Handler {

bool FilterHandler::onConfigure(const Core::Config::Configuration& config)
{
    auto inputName = config.getOr<std::string>("filter.input_channel", "filter_in");
    auto outputName = config.getOr<std::string>("filter.output_channel", "filter_out");
    m_forbiddenWord = config.getOr<std::string>("filter.drop_pattern", "DROP_ME");

    auto inChan = backend().createInputChannel(inputName, config);
    auto outChan = backend().createOutputChannel(outputName, config);
    if (!inChan || !outChan) {
        Proxy::Log::error("[FilterHandler] Failed to create channels");
        return false;
    }

    registerInputChannel(std::move(inChan));
    m_outputChannel = registerOutputChannel(std::move(outChan));
    return true;
}

Backend::MessageResult FilterHandler::onMessageReceived(
    std::string_view channelName,
    const Backend::MessageView& message)
{
    auto payloadSpan = message.payload();
    std::string_view payloadStr(reinterpret_cast<const char*>(payloadSpan.data()), payloadSpan.size());

    // Filter condition:
    if (payloadStr.find(m_forbiddenWord) != std::string_view::npos) {
        Proxy::Log::warn("[FilterHandler] Message rejected: contains forbidden keyword");
        return Backend::Dropped {
            .reason = "Payload contains forbidden pattern",
            .errorCode = 403
        };
    }

    // Forward valid message intact (preserving envelope header & metadata)
    if (m_outputChannel) {
        m_outputChannel->send(message.rawBytes());
        return Backend::Forwarded { };
    }

    return Backend::Consumed { };
}

} // namespace Proxy::Handler
```

### Step 5: Add Subdirectory to `handler/CMakeLists.txt`
In `handler/CMakeLists.txt`, add:
```cmake
add_subdirectory(filter_handler)
```

---

## 3. Writing Unit Tests with Catch2 & `SimulationBackend`

In `test/TestFilterHandler.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "handler/filter_handler/include/FilterHandler.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "backend/message/MessageView.hpp"

TEST_CASE("FilterHandler drops forbidden messages and forwards valid ones", "[handler][filter]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::FilterHandler handler;
    handler.setBackend(&backend);

    Core::Config::Configuration config;
    config.set("filter.input_channel", "test_in");
    config.set("filter.output_channel", "test_out");
    config.set("filter.drop_pattern", "BAD_PACKET");
    REQUIRE(handler.configure(config));

    SECTION("Valid payload is forwarded") {
        std::string validText = "NORMAL_PACKET_CONTENT";
        auto env = Backend::MessageEnvelope::packRaw(Common::Memory::ByteView(validText));
        auto view = Backend::MessageView::from(env);

        auto result = handler.onMessageReceived("test_in", *view);
        REQUIRE(std::holds_alternative<Backend::Forwarded>(result));
    }

    SECTION("Forbidden payload is dropped with errorCode 403") {
        std::string badText = "CONTAINS_BAD_PACKET_HERE";
        auto env = Backend::MessageEnvelope::packRaw(Common::Memory::ByteView(badText));
        auto view = Backend::MessageView::from(env);

        auto result = handler.onMessageReceived("test_in", *view);
        REQUIRE(std::holds_alternative<Backend::Dropped>(result));
        auto dropped = std::get<Backend::Dropped>(result);
        CHECK(dropped.errorCode == 403);
    }
}
```

---

## 4. Declaring the Handler in Pipeline Chains

Add the new handler to a pipeline `.toml` specification (e.g. `chains/my_chain.toml`):

```toml
backend = "shm"

[config]
"channel.stage1.path" = "/tmp/proxy_shm_stage1.dat"
"channel.stage2.path" = "/tmp/proxy_shm_stage2.dat"

[[handlers]]
name = "ingest_stream"
type = "dummy_send"
output_channel = "stage1"

[[handlers]]
name = "content_filter"
type = "filter_handler"
input_channel = "stage1"
output_channel = "stage2"
[handlers.config]
"filter.drop_pattern" = "TEST_DROP"

[[handlers]]
name = "output_printer"
type = "print_handler"
input_channel = "stage2"
```

Run the pipeline:
```bash
./scripts/chain_runner.py chains/my_chain.toml
```

---

## 5. Implementing and Registering a New Transport Backend

To add a new transport backend (for example, `backend/rdma/` or `backend/nats/`):

### Step 1: Create the Backend Directory Structure
Create a dedicated subfolder under `backend/<name>/` (e.g., `backend/rdma/`):
```
backend/rdma/
├── CMakeLists.txt
├── conanfile.py
├── include/
│   └── rdma/
│       ├── RdmaBackend.hpp
│       ├── RdmaInputChannel.hpp
│       └── RdmaOutputChannel.hpp
└── src/
    ├── RdmaBackend.cpp
    ├── RdmaInputChannel.cpp
    └── RdmaOutputChannel.cpp
```

### Step 2: Implement Channels and Backend Contract
1. **`IInputChannel`**: Implement `receive(std::chrono::milliseconds timeout)` and optional batch reception returning `ReceiveResult`.
2. **`IOutputChannel`**: Implement `send(Common::Compat::span<const std::byte> message)` and batch transmission.
3. **`IBackend`**: Implement `createInputChannel(std::string_view name, const IConfig& config)`, `createOutputChannel(std::string_view name, const IConfig& config)`, and `configure(const IConfig& config)`. Provide the exported factory `Backend::createBackend()`.

### Step 3: Write Backend `CMakeLists.txt`
In `backend/<name>/CMakeLists.txt`, declare the library target using globbing and link `backend-interface::backend-interface` and `proxylog::proxylog`:
```cmake
cmake_minimum_required(VERSION 4.0)
project(backend-rdma LANGUAGES CXX)

if(NOT TARGET backend-interface::backend-interface)
  find_package(backend-interface REQUIRED)
endif()

if(NOT TARGET proxylog::proxylog AND NOT TARGET proxylog)
  find_package(proxylog REQUIRED)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(backend-rdma ${SOURCES})

target_include_directories(backend-rdma
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(backend-rdma
    PUBLIC
        backend-interface::backend-interface
        proxylog::proxylog
)

target_compile_features(backend-rdma PUBLIC cxx_std_${CMAKE_CXX_STANDARD})

if(NOT TARGET backend-rdma::backend-rdma)
  add_library(backend-rdma::backend-rdma ALIAS backend-rdma)
endif()
```

### Step 4: Write Backend `conanfile.py`
Create `backend/<name>/conanfile.py` to package the backend into the Conan cache and set the backend properties for dynamic discovery:
```python
import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy

class BackendRdmaConan(ConanFile):
    name = "backend-rdma"
    version = "0.1.0"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "src/*", "include/*"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("backend-interface/0.1.0", transitive_headers=True)
        self.requires("proxylog/0.1.0")

    def package_info(self):
        self.cpp_info.libs = ["backend-rdma"]
        self.cpp_info.set_property("cmake_file_name", "backend-rdma")
        self.cpp_info.set_property("cmake_target_name", "backend-rdma::backend-rdma")
        self.cpp_info.set_property("proxy_component", "backend")
        self.cpp_info.set_property("backend_name", "rdma")
```

### Step 5: Register with Root `conanfile.py`
In the root `conanfile.py`, register the new option and dependency requirement:
1. Under `options` and `default_options`:
   ```python
   "backend_rdma": [True, False],
   ```
   ```python
   "backend_rdma": True,
   ```
2. Under `requirements(self)`:
   ```python
   if self.options.backend_rdma:
       self.requires("backend-rdma/0.1.0")
   ```

*Note: Root `conanfile.py` automatically detects and exports any in-tree recipes (`backend/*`) into the local Conan cache at configure time via `ProxyConan.init()`. You do not need to run manual `conan export` commands.*

### Step 6: Configure and Build
Configure and build with CMake. Conan's generator writes `ConanBackends.cmake`, and `proxy_discover_backends()` automatically binds `backend-rdma` to generate `<handler_name>_rdma` for every handler in the codebase:
```bash
# Configure (e.g. Debug preset)
cmake --preset debug

# Build all targets (or a specific handler with the new backend)
cmake --build --preset debug

# Or build the specific handler with the new backend
cmake --build --preset debug --target print_handler_rdma

# Verify that the target was generated and compiled
./build/debug/bin/print_handler_rdma --help
```
*Tip: If you are building with manual CMake commands instead of presets, pass `-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake` during configuration.*



