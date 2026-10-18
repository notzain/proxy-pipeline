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
