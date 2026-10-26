# Vectorized Ingress Batch Safety: BufferSlot

## Overview
This document outlines the design and implementation of `Backend::BufferSlot` to eliminate parallel synchronized span parameters (`destinations` and `outSizes`) in `IInputChannel::receiveBatch()` and `ISocketPoller::receiveBatch()`.

---

## Design Decisions

1. **`Backend::BufferSlot`**:
   - Location: `backend/interface/include/backend/BufferSlot.hpp`.
   - Structure:
     ```cpp
     struct BufferSlot {
         std::span<std::byte> buffer;
         std::size_t size { 0 };

         [[nodiscard]] constexpr std::span<const std::byte> filledSpan() const noexcept {
             return buffer.subspan(0, size);
         }

         [[nodiscard]] constexpr std::span<std::byte> filledSpan() noexcept {
             return buffer.subspan(0, size);
         }
     };
     ```
   - Rationale: Combines destination target buffer memory and received byte count into a single cohesive struct. Prevents mismatched buffer/size lengths at compile time.

2. **`IInputChannel::receiveBatch` & `ISocketPoller::receiveBatch`**:
   - Updated signature:
     ```cpp
     virtual std::size_t receiveBatch(
         std::span<BufferSlot> slots,
         Timeout timeout);
     ```
   - Eliminates redundant parallel array management across:
     - `IInputChannel` base and implementations (`SocketInputChannel`, `UdsInputChannel`, `ShmInputChannel`, `SimInputChannel`, `UdpInputChannel`, `MockInputChannel`)
     - `ISocketPoller` implementations (`KqueueSocketPoller`, `PosixSocketPoller`, `IoUringSocketPoller`)
     - Threading strategies (`SequentialStrategy`, `DedicatedThreadStrategy`, `ThreadPoolStrategy`)

---

## Tasks / Checklist

- [x] **1. Create `BufferSlot` Type**
  - [x] Add `backend/interface/include/backend/BufferSlot.hpp`.
  - [x] Export `backend/interface` recipe with conan.
- [x] **2. Update `IInputChannel` and `ISocketPoller` Interfaces**
  - [x] Update `IInputChannel.hpp` and `IInputChannel.cpp`.
  - [x] Update `ISocketPoller.hpp` and `ISocketPoller.cpp`.
- [x] **3. Update Platform Socket Pollers**
  - [x] Update `KqueueSocketPoller.hpp` and `.cpp`.
  - [x] Update `PosixSocketPoller.hpp` and `.cpp`.
  - [x] Update `IoUringSocketPoller.hpp` and `.cpp`.
- [x] **4. Update Transport Channels**
  - [x] `backend/socket/` (`SocketInputChannel`).
  - [x] `backend/uds/` (`UdsInputChannel`).
  - [x] `backend/shm/` (`ShmInputChannel` and `ShmRingBuffer`).
  - [x] `backend/sim/` (`SimInputChannel`).
  - [x] `core/net/` (`UdpInputChannel`).
  - [x] `core/simulation/` (`MockInputChannel`).
- [x] **5. Update Threading Strategies & Tests**
  - [x] `SequentialStrategy.cpp`.
  - [x] `DedicatedThreadStrategy.cpp`.
  - [x] `ThreadPoolStrategy.cpp`.
  - [x] `test/TestChannels.cpp`, `test/TestShmBackend.cpp`, `test/TestUdpIngress.cpp`.
- [x] **6. Build, Test & Format**
  - [x] Verify build with `cmake --build build/gcc16-debug`.
  - [x] Run test suite with `ctest --test-dir build/gcc16-debug --output-on-failure` (70/70 passing).
  - [x] Format with `clang-format`.
