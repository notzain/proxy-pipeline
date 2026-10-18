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

| Stage / Component | Baseline (`std::string`) | Option 1 (`std::string_view` Zero-Copy) | Delta (%) |
| :--- | :---: | :---: | :---: |
| **XML Deserialization** | 625.31 ns | **554.00 ns** | **-11.4% (faster)** |
| **Domain Validation** | 12.70 ns | **12.21 ns** | **-3.9% (faster)** |
| **Full Ingress Pipeline** | 1,245.39 ns (1.25 µs) | — | — |

---

## 3. Analysis of Option 1 (Zero-Copy String Views)

1. **Memory Allocation**:
   - `Order` (`std::string` fields) requires dynamic allocation for string members exceeding SSO (Small String Optimization), and in general causes per-field string copies during DOM-to-struct mapping.
   - `OrderView` (`std::string_view` fields) binds non-owning pointer/length slices directly to string values held during parsing, eliminating heap allocations for struct member population.
2. **Deserialization Throughput**:
   - Baseline deserialization runs at $\approx 1.60\text{ M ops/s}$.
   - Option 1 deserialization runs at $\approx 1.81\text{ M ops/s}$ (**+13.1% higher throughput**).
3. **Domain Validation Throughput**:
   - Both operate at sub-13 nanosecond latencies ($\approx 82\text{ M ops/s}$), as both `std::string` and `std::string_view` iterate directly over contiguous character memory.

---

## 4. Next Optimization Targets

- **Target 2 (Character Lookup Table)**: Replace character-by-character `std::isalpha` / `std::isalnum` with compile-time 256-byte bitmap tables.
- **Target 3 (Direct DOM Traversal)**: Skip intermediate `Value` (IR map) construction and populate `OrderView` directly from `pugi::xml_node`.
- **Target 4 (Cached Channel Pointer)**: Bypass hash table lookup and mutex locking on output channel dispatch in `onMessageReceived`.

