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

| Stage / Component | Baseline (`std::string`) | Option 1 (`std::string_view`) | Option 2 (Bitmap Table) | Option 3 (Direct DOM) | Total $\Delta$ (Baseline $\to$ Optimized) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **XML Deserialization** | 585.92 ns | 570.70 ns (-2.6%) | 570.70 ns (0.0%) | **171.18 ns** (-70.0%) | **-70.8%** |
| **Domain Validation** | 12.21 ns | 12.56 ns (+2.9%) | **9.65 ns** (-23.2%) | 9.65 ns (0.0%) | **-21.0%** |
| **Full Ingress Pipeline** | 1,079.45 ns (1.08 µs) | — | — | — | — |

---

## 3. Analysis of Optimization Steps

### Option 1 (Zero-Copy String Views)
1. **Memory Allocation**:
   - `Order` (`std::string` fields) causes per-field string copies during DOM-to-struct mapping.
   - `OrderView` (`std::string_view` fields) binds non-owning pointer/length slices directly to string values held during parsing, eliminating heap copies for struct member population.
2. **Deserialization Throughput**:
   - Baseline deserialization runs at $\approx 1.71\text{ M ops/s}$.
   - Option 1 deserialization runs at $\approx 1.75\text{ M ops/s}$ (**zero-copy view**).

### Option 2 (Lookup Table Domain Validation)
1. **Instruction Efficiency**:
   - Replacing `std::isalpha`, `std::isalnum`, and `std::isspace` with compile-time 256-byte character bitmaps (`CustomerCharTable` and `ItemCharTable`) eliminates runtime branching and function calls.
   - Validation time dropped from **12.21 ns** down to **9.65 ns** (**-21.0% reduction in latency** vs baseline, **-23.2%** vs Option 1).
   - Validates at an extreme **$\approx 103.6\text{ million orders/sec}$** per CPU core.

### Option 3 (Direct DOM Traversal & In-Place Parsing)
1. **Bypassing Intermediate Representation (IR)**:
   - Baseline and Option 1 relied on `Core::Serialization::XmlDeserializer`, which builds an intermediate `Value` IR map/tree before mapping into the model struct.
   - Option 3 uses `pugi::xml_document::load_buffer_inplace` and queries child nodes (`<customer>`, `<item>`, `<quantity>`) directly, coupled with zero-allocation `std::from_chars` for integer parsing.
2. **Throughput & Latency Impact**:
   - Deserialization latency dropped dramatically from **570.70 ns** to **171.18 ns** (**-70.0% reduction** compared to Option 1, **-70.8% overall reduction** compared to baseline).
   - Deserialization throughput jumped from $\approx 1.7\text{ M ops/s}$ to **$\approx 5.84\text{ million orders/sec}$** on a single core.

---

## 4. Next Optimization Targets

- **Target 4 (Cached Channel Pointer / Ingress Zero-Copy)**: Integrate Option 3 + Option 2 directly into `onMessageReceived` and bypass hash table lookup on output channel dispatch.

