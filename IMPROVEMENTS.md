# System Improvements & Architectural Roadmap

Tracking checklist for ongoing performance optimizations, transport backend enhancements, architecture features, and observability tooling.

---

## 1. Concurrency & Execution Strategies

- [x] **Lock-Free Multi-Producer Multi-Consumer Queue (`Core::Queue::MpmcQueue`)**
  - High-performance bounded Vyukov ring buffer with cacheline alignment and ticket sequence counters.
  - Tested and integrated into `ThreadPoolStrategy`.
- [x] **Zero-Copy Task Recycling via `BufferPool`**
  - Lock-free power-of-two pooled memory slab avoiding heap allocations for task buffering.
  - Integrated into `ThreadPoolStrategy` worker message dispatch.
- [ ] **Direct Zero-Copy Ingress Task Forwarding via `BufferPool` across all Strategies**
  - Extend `Core::Memory::BufferPool` and `BufferSlice` into `DedicatedThreadStrategy` and `SequentialStrategy`.
  - Eliminate dynamic heap allocations when handling jumbo payloads (>2048 bytes).
- [ ] **Contiguous Batch Ring Publishing**
  - Add contiguous block reservation/publishing to `SpscQueue` / `MpmcQueue` to enqueue full `receiveBatch` vectors in a single atomic sequence bump.

---

## 2. Transport & Backend Enhancements

- [x] **Pipelined Batching & Vectorized Egress (`sendBatch`)**
  - Added `IOutputChannel::sendBatch(std::span<const std::span<const std::byte>>)` across all transports (`backend-socket`, `backend-uds`, `backend-shm`, `backend-file`).
  - Integrated into all handlers (`MergeHandler`, `SplitHandler`, `PrintHandler`, `OrderValidationHandler`, `XmlVerificationHandler`, `UdpIngressHandler`).
- [x] **Kernel Bypass / Zero-Copy Shared Memory Transport (`backend-shm`)**
  - POSIX shared memory ring buffer (`ShmInputChannel`, `ShmOutputChannel`) delivering **~750k–790k msg/s** and **~58–61 MB/s** with 100% reliability.
- [x] **Platform-Native Vectorized Socket Ingress (`ISocketPoller::receiveBatch`)**
  - macOS / BSD: `KqueueSocketPoller` speculative bulk drainage.
  - Linux: `IoUringSocketPoller` completion queue batch drainage.
  - Portable POSIX: `PosixSocketPoller` non-blocking speculative loop.
- [ ] **Unified `io_uring` Asynchronous Backend (Linux)**
  - Unify disk file I/O, socket recv/send, and timer events into a single `io_uring` instance, removing thread-per-channel polling.
- [x] **Futex / Eventfd Kernel Bypass Signaling in `backend-shm`**
  - Upgraded atomic spin-wait / yield polling in `backend-shm` with cross-process signaling (`ShmSignaler` using Linux `SYS_futex` and process-shared `pthread_cond` on POSIX/macOS) for near-zero idle CPU consumption and microsecond-level wakeups under burst boundaries.

---

## 3. Architecture & Core Pipeline Features

- [x] **Strict Binary Message Envelope & Zero-Copy Views**
  - Introduced `Core::Message::MessageEnvelope` with binary metadata headers (client socket address, timestamp, sequence numbers) and non-owning `MessageView`.
- [x] **Direct Output Channel Egress & Privatization of `IHandler`**
  - Deleted legacy `send(channelName, ...)` indirection and removed hash table lookups / mutex acquisition on every egress write.
  - Privatized all `IHandler` fields, enforcing clean encapsulation.
- [x] **Deduplicated Small-Buffer Span Allocation (`Core::Memory::SmallSpanBuffer`)**
  - Stack-first 64-element span buffer with transparent heap spillover, removing repetitive inline stack arrays across handlers.
- [ ] **Backpressure & Flow Control Protocol**
  - Implement reactive capacity signaling (`canSend()`, `onBackpressure()`, credit-based / token bucket rate limiting) across `IOutputChannel` and `IInputChannel`.
  - Prevent packet drops under unpaced burst workloads on UDP and ring buffers.
- [x] **Content-Based Dynamic Router Handler (`route_handler`)**
  - Enterprise integration pattern (EIP) router inspecting `MessageView` metadata (client ports, protocol IDs) and payload patterns (contains, prefix) with vectorized batch bucket dispatching to configured egress channels.

---

## 4. Observability & Developer Experience

- [x] **Lock-Free Latency & Telemetry Metrics**
  - Lightweight atomic counters (`Counter`, `Gauge`), lock-free cumulative latency histogram (`Histogram`), thread-safe `MetricsRegistry`, Prometheus `/metrics` exposition format, and `StatsDExporter` UDP datagram sender.
- [x] **Deterministic Simulation & Mock Channel Replay**
  - Native zero-dependency PCAP parser (`PcapReader`) and writer (`PcapWriter`) supporting microsecond/nanosecond captures and UDP packet reconstruction.
  - Virtual simulation clock (`SimulationClock`) supporting stepped deterministic time and realtime modes.
  - Mock transport channels (`MockInputChannel`, `MockOutputChannel`) and mock transport backend (`SimulationBackend`) for regression test suites and cycle-accurate pipeline simulation.

