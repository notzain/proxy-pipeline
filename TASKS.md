#Tasks& Architectural Refactoring Roadmap

This document outlines structural refactoring, interface decoupling, and abstraction improvements across `backend/` and `core/`, organized by design principles and prioritized for incremental implementation in dedicated feature branches.

---

## 1. Domain Decoupling & Interface Cleanliness

- [~] **Relocate `MessageResult` out of `backend/interface` into `core/message/` (Skipped / Retained in `backend/interface`)**
  - **Decision**: Keep `MessageResult` in `backend/interface` because `backend-sim` links `backend-interface` and must remain independent of `core`. Keeps transport-level tracing cohesive without circular dependencies.

- [x] **Invert `IInputChannel` Hierarchy to Prioritize Zero-Copy `receiveInto`**
  - **Action**: Made `receiveInto(destination, timeout)` the pure virtual primitive. Implemented `receive(timeout)` as non-virtual default wrapper. Removed duplicate overrides across all backends. (Completed in `f29dc3c`).

- [~] **Retain `MessageResult` & Direct Simulation Lifecycle on `IBackend` (Skipped / Working as Intended)**
  - **Decision**: Keep `MessageResult` in `backend/interface` and simulation tracing methods directly on `Backend::IBackend`. Pragmatically keeps `backend-sim`, all 10 handler executables, and the Python test coordinator aligned without circular dependencies or extra wiring layers.

- [ ] **Consolidate Duplicate UDP Channels (`core/net/` vs `backend/socket/`)**
  - **Issue**: `core/include/core/net/UdpInputChannel.hpp` & `UdpOutputChannel.hpp` duplicate functionality found in `backend/socket/src/SocketInputChannel.cpp` & `SocketOutputChannel.cpp`.
  - **Action**: Unify socket primitives. Keep transport-specific socket channels in `backend/socket/` and have `core/net/` provide only low-level, non-channel socket networking primitives (if needed) or remove the redundant channel classes from `core/net/`.

---

## 2. Handler Single Responsibility & Decomposition

- [x] **Extract Output Channel Routing & Registration (`ChannelRegistry`)**
  - **Action**: Made `IInputChannel` and `IOutputChannel` self-identifying via base `name()`. Simplified `IHandler` to use direct `registerOutputChannel(std::unique_ptr<IOutputChannel>)` storing a flat vector of owned output channels. Removed dead string maps, locks, and redundant `channelName` member variables across handlers (e.g. `MergeHandler`).

- [x] **Decouple Event Loop Lifecycle from `IHandler` (`HandlerRunner` / `HandlerRuntime`)**
  - **Action**: Separated execution lifecycle orchestration, thread strategies, and POSIX signal handling into `HandlerRunner`. Modeled `ExecutionModel` as a strongly-typed `std::variant<SequentialModel, DedicatedThreadModel, ThreadPoolModel>` encapsulating model-specific properties (such as pool size and strict FIFO ordering). `IHandler` now focuses strictly on message processing callbacks (`onMessageReceived`, `onBatchReceived`, `step`).

- [x] **Decouple Telemetry Configuration from `IHandler::configure`**
  - **Action**: Moved telemetry StatsD exporter instantiation and lifecycle configuration into `HandlerRunner::configure` and `HandlerRunner::configureTelemetry`. All handler main executables, unit tests, and benchmarks now configure and run via `HandlerRunner`.

---

## 3. Strongly-Typed Values & Allocation Safety

- [ ] **Introduce `ChannelId` Strong Value Type**
  - **Issue**: Channels are keyed and looked up using raw `std::string` and `std::string_view` across `IHandler`, `IBackend`, and `MessageResult`. This risks mixing arbitrary strings (e.g. config keys, host names) with channel names.
  - **Action**: Create a lightweight `ChannelId` value class (with constexpr construction, transparent hashing, and string view conversion) to provide type safety and avoid accidental allocations.

- [x] **Encapsulate `IHandler` Input Channel Collection**
  - **Issue**: `IHandler::getInputChannels()` exposed internal ownership storage (`std::unique_ptr<IInputChannel>`) and its container layout, while a separate registration-name field duplicated `IInputChannel::name()`.
  - **Action**: Store input channels directly as `std::unique_ptr<IInputChannel>` and use `inputChannelCount()` / `forEachInputChannel(Callback)` to access the private collection. Dispatch uses the name owned by each channel.

- [x] **Cohesive Batch Buffer Value Type (`BufferSlot` / `BatchSpan`)**
  - **Issue**: `IInputChannel::receiveBatch` requires two parallel spans (`std::span<const std::span<std::byte>> destinations` and `std::span<std::size_t> outSizes`) that must stay synchronized in length.
  - **Action**: Created `Backend::BufferSlot` uniting destination memory span with transferred byte count:
    ```cpp
    struct BufferSlot {
        std::span<std::byte> buffer;
        std::size_t size { 0 };
    };
    ```
    Replaced parallel spans with `std::span<BufferSlot>` across `IInputChannel`, `ISocketPoller`, all backend channels (`socket`, `uds`, `shm`, `sim`, `net`, `simulation`), pollers, and execution strategies.

    -- -

    ##4. Interface
    Segregation(ISP)
    & Ingress Ergonomics

        - [x] * *Invert `IInputChannel` Ingress Hierarchy(Zero - Copy as First - Class) * *-**Issue** : `receive(Timeout)` returning `std::optional<std::vector<std::byte>>`
        requires heap
    allocation and was a pure virtual method,
    whereas `receiveInto` was a fallback virtual method.- **Action** : Made in - place `receiveInto(std::span<std::byte>, Timeout)` the core pure virtual primitive.Provided `receive(Timeout)` as a non - virtual convenience wrapper method on `IInputChannel` delegating to `receiveInto`.Removed duplicated `receive` overrides across all transport backends(`file`, `socket`, `uds`, `shm`, `sim`, `net`, `simulation`).
