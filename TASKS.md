# Tasks & Architectural Refactoring Roadmap

This document outlines structural refactoring, interface decoupling, and abstraction improvements across `backend/` and `core/`, organized by design principles and prioritized for incremental implementation in dedicated feature branches.

---

## 1. Domain Decoupling & Interface Cleanliness

- [ ] **Relocate `MessageResult` out of `backend/interface` into `core/message/`**
  - **Issue**: `backend/interface/include/backend/MessageResult.hpp` defines application-level handler outcomes (`Forwarded`, `Consumed`, `Dropped`). Transport channels (`IInputChannel`, `IOutputChannel`) transmit raw byte spans (`std::span<std::byte>`) and should have no dependency on handler execution outcomes.
  - **Action**: Move `Forwarded`, `Consumed`, `Dropped`, and `MessageResult` entirely into `core/include/core/message/MessageResult.hpp` (namespace `Core::Message`). Remove `backend/interface/include/backend/MessageResult.hpp`. Update consuming handlers and backends.

- [ ] **Extract Message Tracing & Observer Hooks from `IBackend` into `IMessageObserver`**
  - **Issue**: `Backend::IBackend` mixes transport channel factory methods (`createInputChannel`, `createOutputChannel`, `configure`) with simulation/tracing hooks (`tracksMessageResults`, `onMessageResult`, `onHandlerStarted`). Network and IPC backends (`SocketBackend`, `ShmBackend`, `UdsBackend`, `FileBackend`) are forced to implement no-op stubs.
  - **Action**: Define an abstract `IMessageObserver` / `ITraceListener` interface in `core/` (or `backend/interface/` if transport-neutral). `SimulationBackend` implements `IMessageObserver`. Inject `IMessageObserver*` into `IHandler` or the execution strategy rather than polling through `IBackend`.

- [ ] **Consolidate Duplicate UDP Channels (`core/net/` vs `backend/socket/`)**
  - **Issue**: `core/include/core/net/UdpInputChannel.hpp` & `UdpOutputChannel.hpp` duplicate functionality found in `backend/socket/src/SocketInputChannel.cpp` & `SocketOutputChannel.cpp`.
  - **Action**: Unify socket primitives. Keep transport-specific socket channels in `backend/socket/` and have `core/net/` provide only low-level, non-channel socket networking primitives (if needed) or remove the redundant channel classes from `core/net/`.

---

## 2. Handler Single Responsibility & Decomposition

- [ ] **Extract Output Channel Routing & Registration (`ChannelRegistry`)**
  - **Issue**: `Core::Handler::IHandler` currently manages input channels, output channels (`m_outputChannels`, `m_outputMutex`, `TransparentStringHash`), telemetry exporting, and thread execution strategies.
  - **Action**: Extract channel storage, thread-safe registration, and lookup into a dedicated `ChannelRegistry` or `ChannelRouter` class. `IHandler` consumes the registry or exposes clean delegation methods.

- [ ] **Decouple Event Loop Lifecycle from `IHandler` (`HandlerRunner` / `HandlerRuntime`)**
  - **Issue**: Lifecycle management (`start`, `startAsync`, `wait`, `stop`, signal handling for SIGINT/SIGTERM, execution strategy configuration) is tightly coupled with message processing logic inside `IHandler`.
  - **Action**: Separate execution lifecycle orchestration into a runner/supervisor component (`HandlerRunner`). `IHandler` focuses strictly on message processing callbacks (`onMessageReceived`, `onBatchReceived`, `step`). Simplifies unit testing by removing background strategy/signal dependencies from test suites.

- [ ] **Decouple Telemetry Configuration from `IHandler::configure`**
  - **Issue**: `IHandler::configureTelemetry` and the owned `std::unique_ptr<Telemetry::StatsDExporter>` sit inside the base handler class, mixing telemetry transport with application routing.
  - **Action**: Move telemetry exporter instantiation and lifecycle to the runner/pipeline composition layer, passing telemetry sinks to handlers as needed.

---

## 3. Strongly-Typed Values & Allocation Safety

- [ ] **Introduce `ChannelId` Strong Value Type**
  - **Issue**: Channels are keyed and looked up using raw `std::string` and `std::string_view` across `IHandler`, `IBackend`, and `MessageResult`. This risks mixing arbitrary strings (e.g. config keys, host names) with channel names.
  - **Action**: Create a lightweight `ChannelId` value class (with constexpr construction, transparent hashing, and string view conversion) to provide type safety and avoid accidental allocations.

- [ ] **Encapsulate `NamedInputChannel` Collection**
  - **Issue**: `IHandler::getInputChannels()` exposes `const std::vector<NamedInputChannel>&`, exposing internal ownership structures (`std::unique_ptr<IInputChannel>`).
  - **Action**: Encapsulate input channels inside an `InputChannelSet` or expose query methods (e.g., `findInputChannel(ChannelId)`, `forEachInputChannel(Callback)`).

- [ ] **Cohesive Batch Buffer Value Type (`BufferSlot` / `BatchSpan`)**
  - **Issue**: `IInputChannel::receiveBatch` requires two parallel spans (`std::span<const std::span<std::byte>> destinations` and `std::span<std::size_t> outSizes`) that must stay synchronized in length.
  - **Action**: Bundle target buffer spans with transferred byte counts into a single struct:
    ```cpp
    struct BufferSlot {
        std::span<std::byte> buffer;
        std::size_t bytesTransferred { 0 };
    };
    ```
    Pass a single `std::span<BufferSlot>` to `receiveBatch`.

---

## 4. Interface Segregation (ISP) & Ingress Ergonomics

- [x] **Invert `IInputChannel` Ingress Hierarchy (Zero-Copy as First-Class)**
  - **Issue**: `receive(Timeout)` returning `std::optional<std::vector<std::byte>>` requires heap allocation and was a pure virtual method, whereas `receiveInto` was a fallback virtual method.
  - **Action**: Made in-place `receiveInto(std::span<std::byte>, Timeout)` the core pure virtual primitive. Provided `receive(Timeout)` as a non-virtual convenience wrapper method on `IInputChannel` delegating to `receiveInto`. Removed duplicated `receive` overrides across all transport backends (`file`, `socket`, `uds`, `shm`, `sim`, `net`, `simulation`).

