# XML Verification Pipeline Benchmarks

Performance tracking across optimizations and execution strategies.

**Hardware & OS**: macOS (Apple Silicon / arm64)  
**Compiler**: AppleClang / GCC (Debug with optimizations / C++20)  
**Message Size**: 82 bytes (Standard XML Order document)  
**Schema**: `order.xsd` (3 elements: customer, item, quantity)

---

## 1. Baseline Performance (Initial Socket + Mutex Implementation)

- **Test Size**: 20,000 messages
- **Characteristics**:
  - `XmlVerificationHandler` uses single `std::mutex` around `xmlSchemaValidateDoc`.
  - `SocketInputChannel` allocates a fresh 64 KB `Buffer(65535)` on every `receive()` call.
  - Per-message forward logging set to `debug`.

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 20,000 | 109,800.82 | 8.59 | 9.00 | 27.00 |
| **UDP (Dedicated)** | 20,000 | 54,018.72 | 4.22 | 121,345.46 | 227,086.00 |
| **UDP (Sequential)** | 20,000 | 56,012.84 | 4.38 | 123,167.21 | 230,638.00 |
| **UDP (ThreadPool 4)** | 20,000 | 56,609.59 | 4.43 | 976.44 | 3,523.00 |

---

## 2. Optimization Phase 1: Lock-Free Contexts & Zero 64KB Allocations

- **Changes**:
  1. **Thread-Local Schema Validation Contexts (`thread_local xmlSchemaValidCtxtPtr`)**: Removed global `std::mutex` from `XmlVerificationHandler::validate()`, allowing worker threads in `ThreadPoolStrategy` to validate concurrent documents in parallel.
  2. **Thread-Local Scratch Ingress Buffer**: Replaced the 64 KB `std::vector` heap allocation per packet in `SocketInputChannel::receive()` with a `thread_local std::array<std::byte, 65535>` scratch buffer, only allocating a vector sized to the exact bytes received.
  3. **Cached Destination Address & 4MB Send Buffer**: Pre-resolved destination `sockaddr_in` once during `SocketOutputChannel::openSocket()` and enabled 4MB `SO_SNDBUF`.

- **Test Size**: 20,000 messages

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) | Delta vs Baseline |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 20,000 | 154,374.59 | 12.07 | 6.39 | 18.00 | **+40.6%** |
| **UDP (Dedicated)** | 20,000 | 59,169.32 | 4.63 | 111,135.45 | 210,153.00 | **+9.5%** |
| **UDP (Sequential)** | 20,000 | 60,880.39 | 4.76 | 112,562.72 | 207,233.00 | **+8.7%** |
| **UDP (ThreadPool 4)** | 20,000 | **62,559.04** | **4.89** | 2,959.74 | 7,710.00 | **+10.5%** |
---

## 3. Optimization Phase 2: Work Queue Batching & ThreadPool Scaling

- **Changes**:
  1. **Worker Batch Dequeue**: Worker threads in `ThreadPoolStrategy` drain up to 16 tasks per lock acquisition (`m_taskQueue`) instead of locking and unlocking the mutex per message.
  2. **ThreadPool 8 Scaling**: Tested ThreadPool with 8 workers to match 8 physical/efficiency CPU cores on Apple Silicon.

- **Test Size**: 20,000 messages

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) | Latency vs Baseline |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 20,000 | **190,837.87** | **14.92** | 5.17 | 7.00 | **-42.6% latency** |
| **UDP (Dedicated)** | 20,000 | 59,693.65 | 4.67 | 110,528.49 | 207,255.00 | -8.9% latency |
| **UDP (Sequential)** | 20,000 | 60,276.19 | 4.71 | 112,772.35 | 209,821.00 | -8.4% latency |
| **UDP (ThreadPool 4)** | 20,000 | **61,495.89** | **4.81** | 1,867.46 | 5,130.00 | High throughput |
| **UDP (ThreadPool 8)** | 20,000 | 55,190.07 | 4.32 | **449.44** | **1,647.00** | **-54.0% latency (<0.5ms)** |

---

## 4. Release Mode Compiler Optimizations (`-O3 -DNDEBUG`)

- **Configuration**:
  - Compiler: `GCC 16.2.0` (Apple Silicon arm64)
  - Optimization: **`-O3 -DNDEBUG`** (configured via `cmake --preset gcc16-release`)
  - Enables aggressive compiler inlining, loop vectorization, and eliminates debug instrumentation.

### Results (20,000 Messages):

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) | vs Debug (-g) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 20,000 | **301,686.43** | **23.59** | **3.28** | **6.00** | **+58.1% Throughput** |
| **UDP (Dedicated)** | 20,000 | **88,864.40** | **6.95** | **66,201.46** | **119,605.00** | **+48.9% Throughput** |
| **UDP (Sequential)** | 20,000 | **93,470.61** | **7.31** | **62,344.56** | **114,692.00** | **+55.1% Throughput** |
| **UDP (ThreadPool 4)**| 20,000 | **75,021.29** | **5.87** | **4,438.07** | **10,376.00** | **+22.0% Throughput** |
| **UDP (ThreadPool 8)**| 20,000 | **61,523.13** | **4.81** | **23,683.93** | **61,671.00** | Stable scaling |

---

### Results (50,000 Messages Heavy Burst):

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) | Delivery Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 50,000 | **387,344.67** | **30.29** | **2.56** | **4.00** | **100% (0 drops)** |
| **UDP (ThreadPool 4)**| 50,000 | **78,267.47** | **6.12** | **50,835.26** | **93,822.00** | **100% (0 drops)** |
| **UDP (ThreadPool 8)**| 50,000 | **62,985.22** | **4.93** | **86,966.09** | **154,239.00** | **100% (0 drops)** |

---

## 5. Optimization Phase 3: Zero-Allocation Ingress (`receiveInto`) & Typed Task Queue

- **Changes**:
  1. **Direct Span Ingress (`IInputChannel::receiveInto`)**: Introduced `receiveInto(std::span<std::byte> dest, Timeout timeout)` avoiding per-message `Buffer` (`std::vector<std::byte>`) heap construction on ingress.
  2. **`SocketInputChannel::receiveInto`**: Sockets read straight into pre-allocated memory without any intermediate allocations.
  3. **Zero-Heap Event Loops**: `DedicatedThreadStrategy` and `SequentialStrategy` now stream directly into `handler.onMessageReceived(name, span)` using a reusable `thread_local` buffer.
  4. **Typed `MessageTask` Struct in `ThreadPoolStrategy`**: Replaced heap-allocated `std::function<void()>` task closures with a lightweight POD struct (`std::string channelName`, `vector<std::byte> buffer`).

### Results (20,000 Messages, Release `-O3`):

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 20,000 | **398,303.23** | **31.15** | **2.49** | **3.00** |
| **UDP (Dedicated)** | 20,000 | **87,999.89** | **6.88** | **65,841.40** | **119,980.00** |
| **UDP (Sequential)** | 20,000 | **88,137.57** | **6.89** | **66,584.36** | **122,686.00** |
| **UDP (ThreadPool 4)**| 20,000 | **70,730.61** | **5.53** | **21,106.44** | **38,944.00** |
| **UDP (ThreadPool 8)**| 20,000 | **54,622.72** | **4.27** | **21,021.48** | **47,245.00** |

---

### Stress Test (100,000 Messages Massive Burst, Release `-O3`):

| Strategy | Messages | Throughput (m/s) | Throughput (MB/s) | Avg Lat (µs) | p99 (µs) | Delivery Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Direct (XSD)** | 100,000 | **422,765.05** | **33.06** | **2.34** | **3.00** | **100% (0 drops)** |
| **UDP (ThreadPool 8)**| 100,000 | **52,092.07** | **4.07** | **18,560.59** | **80,788.00** | **100% (0 drops)** |
| **UDP (ThreadPool 4)**| 100,000 | **44,540.81** | **3.48** | **20,265.73** | **155,640.00** | **100% (0 drops)** |

> [!NOTE]
> Even under a massive **100,000-packet burst**, both `ThreadPool 4` and `ThreadPool 8` achieved a **100% packet delivery rate (0 drops)** because the zero-allocation ingress path kept the kernel UDP socket buffers thoroughly drained.

---
