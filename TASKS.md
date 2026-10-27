# Architecture Review and Improvement Roadmap

## Assessment

**Overall architecture rating: 7/10**

The project has clear interface, core, backend, and handler layers, with strong use of RAII, spans, batching, and simulation. The most important follow-up areas are concurrency contracts, shutdown safety, dispatch responsibilities, and transport error reporting.

This roadmap captures architectural findings from a representative review of the build structure, core runtime, backend interfaces, handlers, and tests. It is not a line-by-line audit.

## 1. Correctness and Runtime Contracts

- [x] **Preserve channel identity when moving UDP channels**
  - **Issue**: Custom move constructors and move assignments for `Core::Net::UdpInputChannel` and `UdpOutputChannel` do not transfer their `IInputChannel` / `IOutputChannel` base subobjects. The base channel name can therefore become empty after a move.
  - **Action**: Added protected base-interface move operations and invoked them in both UDP gateway channel move constructors and assignments. Tests verify `name()` survives move construction and assignment.
  - **Evidence**: `core/src/net/UdpInputChannel.cpp`; `core/src/net/UdpOutputChannel.cpp`.

- [x] **Define handler and channel thread-safety requirements**
  - **Issue**: Dedicated-thread and thread-pool execution can invoke handler callbacks concurrently, and `step()` runs on a separate thread. The handler and channel interfaces do not clearly state the required thread-safety guarantees.
  - **Action**: Documented each execution model's concurrency guarantees. Parallel handlers must protect shared state by default; `execution.serialize_handler_access = true` serializes `processBatch()` and `step()` without adding mutex acquisition to the default parallel path. Each registered input channel is polled by one strategy thread; shared output channels must support concurrent sends or be synchronized by callers.
  - **Evidence**: `docs/handler-channel-thread-safety.md`; `core/include/core/handler/IHandler.hpp`; `backend/interface/include/backend/IInputChannel.hpp`; `backend/interface/include/backend/IOutputChannel.hpp`; `core/src/threading/DedicatedThreadStrategy.cpp`; `core/src/threading/ThreadPoolStrategy.cpp`.

- [x] **Make signal-triggered shutdown safe**
  - **Issue**: The POSIX signal callback calls `HandlerRunner::stop()` directly. The resulting C++ strategy and thread-stop operations are not guaranteed to be async-signal-safe, and the global active-runner pointer effectively imposes a single-runner signal ownership model.
  - **Action**: The signal callback now sets a lock-free notification flag. A coordinator thread performs strategy shutdown and waiting in normal context. Only one runner may own process signal handling at a time; a conflicting start fails explicitly, and prior signal dispositions are restored.
  - **Evidence**: `docs/handler-signal-safe-shutdown.md`; `core/src/handler/HandlerRunner.cpp`; `test/TestChannels.cpp`.

## 2. Dispatch and Observability

- [x] **Centralize message validation and ingress accounting**
  - **Issue**: The sequential, dedicated-thread, and thread-pool strategies duplicate envelope validation, malformed-message accounting, and batch dispatch. Latency is also observed in the strategy dispatch paths and again in `IHandler::processBatch()`, causing duplicate observations for these paths.
  - **Action**: Centralize envelope validation, malformed-message accounting, ingress message/byte counters, and batch dispatch. Keep message-age latency observation in `IHandler::processBatch()` as the single owner. Keep strategy-specific code focused on scheduling, queueing, and buffer ownership.
  - **Evidence**: `core/src/threading/MessageDispatcher.cpp`; `core/src/threading/SequentialStrategy.cpp`; `core/src/threading/DedicatedThreadStrategy.cpp`; `core/src/threading/ThreadPoolStrategy.cpp`; `core/src/handler/IHandler.cpp`; `test/TestChannels.cpp`; `test/TestTelemetry.cpp`.

## 3. Backend Interface Evolution

- [x] **Distinguish receive timeouts from channel errors**
  - **Issue**: `IInputChannel::receiveInto()` uses zero to indicate timeout/no message, while implementations may also return zero when socket setup or another receive operation fails. Callers cannot reliably distinguish idle channels from failed channels.
  - **Action**: Design a typed receive result that distinguishes data, timeout, closure, and error. Update channels and strategies together, and add tests for each result state.
  - **Evidence**: `backend/interface/include/backend/IInputChannel.hpp`; `core/src/net/UdpInputChannel.cpp`.

- [x] **Specify the message envelope wire format explicitly**
  - **Issue**: The envelope is represented by a packed C++ struct and integer fields are copied in host byte order. This couples the wire representation to compiler/platform layout and endianness assumptions.
  - **Action**: Defined explicit big-endian (network byte order) integer serialization and deserialization in `Core::Message::Wire`, eliminating compiler struct packing and endianness dependencies. Decoupled `MessageView` from fixed headers so handlers consistently access `payload()`, enabling custom backend protocol framing. Added unit tests with golden byte sequence vectors.
  - **Evidence**: `docs/message-envelope-wire-format.md`; `core/include/core/message/WireFormat.hpp`; `core/include/core/message/EnvelopeHeader.hpp`; `core/include/core/message/MessageView.hpp`; `core/src/message/MessageEnvelope.cpp`; `core/src/message/MessageView.cpp`; `test/TestUdpIngress.cpp`.

## Suggested Order

1. Fix channel-name preservation across UDP channel moves.
2. Remove duplicate latency observations and consolidate dispatch behavior.
3. Define and test handler/channel concurrency guarantees.
4. Make signal-triggered shutdown safe.
5. Design typed receive outcomes.
6. Plan an explicitly encoded envelope format as a separate protocol revision.
