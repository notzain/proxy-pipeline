# OrderValidationHandler Micro-Benchmarks

This document records the micro-benchmarks and optimization iterations for the `OrderValidationHandler`, evaluated using Catch2 v3 benchmark utilities (`BENCHMARK(...)`) running in Release mode (`-O3 -DNDEBUG`, Apple Silicon arm64 / GCC 16).

---

## 1. Benchmark Methodology

- **Framework**: Catch2 v3 (`[!benchmark][order_validation]` tag filter).
- **Sampling**: 100 samples per benchmark target, with automatic warm-up and iteration calibration.
- **Payload**: Standard Order XML message matching `order.xsd`:
  ```xml
  <order><customer>Alice Smith</customer><item>Widget-Pro</item><quantity>42</quantity></order>
  ```
- **Execution Command**:
  ```bash
  ./build/gcc16-release/bin/proxy_tests "[order_validation]"
  ```

---

## 2. Benchmark Progression & Impact

### Comparison Table

| Stage / Component | Baseline (`std::string`) | Option 1 (`std::string_view`) | Option 2 (Bitmap Table) | Total $\Delta$ (Baseline $\to$ Optimized) |
| :--- | :---: | :---: | :---: | :---: |
| **XML Deserialization** | 614.77 ns | **574.28 ns** (-6.6%) | 574.28 ns (0.0%) | **-6.6%** |
| **Domain Validation** | 12.09 ns | 12.10 ns (+0.1%) | **9.72 ns** (-19.7%) | **-19.6%** |
| **Full Ingress Pipeline** | 1,245.39 ns (1.25 µs) | — | — | — |

---

## 3. Analysis of Optimization Steps

### Option 1 (Zero-Copy String Views)
1. **Memory Allocation**:
   - `Order` (`std::string` fields) causes per-field string copies during DOM-to-struct mapping.
   - `OrderView` (`std::string_view` fields) binds non-owning pointer/length slices directly to string values held during parsing, eliminating heap copies for struct member population.
2. **Deserialization Throughput**:
   - Baseline deserialization runs at $\approx 1.63\text{ M ops/s}$.
   - Option 1 deserialization runs at $\approx 1.74\text{ M ops/s}$ (**faster and zero-copy**).

### Option 2 (Lookup Table Domain Validation)
1. **Instruction Efficiency**:
   - Replacing `std::isalpha`, `std::isalnum`, and `std::isspace` with compile-time 256-byte character bitmaps (`CustomerCharTable` and `ItemCharTable`) eliminates runtime branching and function calls.
   - Validation time dropped from **12.09 ns** down to **9.72 ns** (**-19.6% reduction in latency**).
   - Validates at an extreme **$\approx 103\text{ million orders/sec}$** per CPU core.

---

## 4. Next Optimization Targets

- **Target 3 (Direct DOM Traversal)**: Skip intermediate `Value` (IR map) construction and populate `OrderView` directly from `pugi::xml_node`.
- **Target 4 (Cached Channel Pointer)**: Bypass hash table lookup and mutex locking on output channel dispatch in `onMessageReceived`.

