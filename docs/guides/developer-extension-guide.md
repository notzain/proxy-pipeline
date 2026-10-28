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
    Core::Message::MessageResult onMessageReceived(
        std::string_view channelName,
        const Core::Message::MessageView& message) override;

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
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageResult.hpp"
#include "core/message/MessageView.hpp"
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

Core::Message::MessageResult FilterHandler::onMessageReceived(
    std::string_view channelName,
    const Core::Message::MessageView& message)
{
    auto payloadSpan = message.payload();
    std::string_view payloadStr(reinterpret_cast<const char*>(payloadSpan.data()), payloadSpan.size());

    // Filter condition:
    if (payloadStr.find(m_forbiddenWord) != std::string_view::npos) {
        Proxy::Log::warn("[FilterHandler] Message rejected: contains forbidden keyword");
        return Core::Message::Dropped {
            .reason = "Payload contains forbidden pattern",
            .errorCode = 403
        };
    }

    // Forward valid message intact (preserving envelope header & metadata)
    if (m_outputChannel) {
        m_outputChannel->send(message.rawBytes());
        return Core::Message::Forwarded { };
    }

    return Core::Message::Consumed { };
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
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"

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
        auto env = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(validText));
        auto view = Core::Message::MessageView::from(env);

        auto result = handler.onMessageReceived("test_in", *view);
        REQUIRE(std::holds_alternative<Core::Message::Forwarded>(result));
    }

    SECTION("Forbidden payload is dropped with errorCode 403") {
        std::string badText = "CONTAINS_BAD_PACKET_HERE";
        auto env = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(badText));
        auto view = Core::Message::MessageView::from(env);

        auto result = handler.onMessageReceived("test_in", *view);
        REQUIRE(std::holds_alternative<Core::Message::Dropped>(result));
        auto dropped = std::get<Core::Message::Dropped>(result);
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

## 5. Implementing a New Transport Backend

To add a new transport (e.g. `backend/rdma/` or `backend/nats/`):

1. **Implement `IInputChannel`**: Provide `receive(span<byte>, timeout)` and `receiveBatch(span<BufferSlot>, timeout)`.
2. **Implement `IOutputChannel`**: Provide `send(span<byte>)` and `sendBatch(span<span<byte>>)`.
3. **Implement `IBackend`**: Factory with `createInputChannel(name, config)` and `createOutputChannel(name, config)`.
4. **Register with Conan and CMake**:
   - Add backend to root `conanfile.py`.
   - Call `proxy_register_backend(backend-newname newname)` in `CMakeLists.txt`.
   - `ProxyBackends.cmake` will automatically discover the backend and generate binaries for every existing handler.

