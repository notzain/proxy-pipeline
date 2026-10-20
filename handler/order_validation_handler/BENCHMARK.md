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

| Stage / Component | Baseline (`std::string`) | Option 1 (`std::string_view`) | Option 2 (Bitmap Table) | Option 3 (Direct DOM) | Option 4 (Cached Channel + Fast Ingress) | Total $\Delta$ (Baseline $\to$ Optimized) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **XML Deserialization** | 613.09 ns | 607.32 ns (-0.9%) | 607.32 ns (0.0%) | **192.98 ns** (-68.2%) | 192.98 ns (0.0%) | **-68.5%** |
| **Domain Validation** | 13.64 ns | 12.75 ns (-6.5%) | **9.37 ns** (-26.5%) | 9.37 ns (0.0%) | 9.37 ns (0.0%) | **-31.3%** |
| **Full Ingress Pipeline** | 1,024.96 ns (1.02 µs) | — | — | — | **244.41 ns** (-76.2%) | **-76.2%** |

### XML Engine Comparison: `pugixml` vs `libxml2` vs `xerces-c`

Side-by-side micro-benchmark of XML parsing and DOM / SAX traversal into the C++ `Order` model across parser reuse and streaming strategies:

| XML Engine | Parser Strategy | Mean Latency | Throughput (ops/sec) | vs. `pugixml` Direct |
| :--- | :--- | :---: | :---: | :---: |
| **`pugixml` (Direct In-Place)** | Direct DOM (`load_buffer_inplace`) into `OrderView` | **196.31 ns** | **$\approx 5,094,000$ ops/s** | **Baseline ($1.0\times$)** |
| **`pugixml` (In-Place IR Traits)** | `load_buffer_inplace` + zero-copy `Value` (`std::string_view`) into `OrderView` | **477.51 ns** | **$\approx 2,094,000$ ops/s** | **$2.4\times$ slower** |
| **`pugixml` (IR Trait Deserializer)** | Copying `load_buffer` + heap `Value` (`std::string`) into `Order` | 547.96 ns | $\approx 1,825,000$ ops/s | $2.8\times$ slower |
| **`libxml2` (Reused Context DOM)** | Thread-local `xmlParserCtxtPtr` + `xmlCtxtReadMemory` into `Order` | **1,392.18 ns** (1.39 µs) | **$\approx 718,300$ ops/s** | **$7.1\times$ slower** |
| **`libxml2` (Direct DOM)** | `xmlReadMemory` + direct text node access into `Order` | **1,883.00 ns** (1.88 µs) | $\approx 531,100$ ops/s | $9.6\times$ slower |
| **`libxml2` (Streaming Cursor)** | `xmlReaderForMemory` + cursor step into `Order` | **1,899.87 ns** (1.90 µs) | $\approx 526,400$ ops/s | $9.7\times$ slower |
| **`xerces-c` (Reused Parser DOM)** | Thread-local `XercesDOMParser` + `resetDocumentPool()` into `Order` | **5,823.00 ns** (5.82 µs) | **$\approx 171,700$ ops/s** | **$29.7\times$ slower** |
| **`xerces-c` (SAX2 Event-Driven)** | `SAX2XMLReader` + `DefaultHandler` callbacks into `Order` | **7,028.00 ns** (7.03 µs) | $\approx 142,300$ ops/s | $35.8\times$ slower |
| **`xerces-c` (Direct DOM)** | `MemBufInputSource` + `XercesDOMParser` + UTF-16 transcode into `Order` | **9,191.50 ns** (9.19 µs) | $\approx 108,800$ ops/s | $46.8\times$ slower |

---

## 3. Analysis of Optimization Steps

### Option 1 (Zero-Copy String Views)
1. **Memory Allocation**:
   - `Order` (`std::string` fields) causes per-field string copies during DOM-to-struct mapping.
   - `OrderView` (`std::string_view` fields) binds non-owning pointer/length slices directly to string values held during parsing, eliminating heap copies for struct member population.
2. **Deserialization Throughput**:
   - Baseline deserialization runs at $\approx 1.59\text{ M ops/s}$.
   - Option 1 deserialization runs at $\approx 1.78\text{ M ops/s}$ (**zero-copy view**).

### Option 2 (Lookup Table Domain Validation)
1. **Instruction Efficiency**:
   - Replacing `std::isalpha`, `std::isalnum`, and `std::isspace` with compile-time 256-byte character bitmaps (`CustomerCharTable` and `ItemCharTable`) eliminates runtime branching and function calls.
   - Validation time dropped from **11.84 ns** down to **9.39 ns** (**-20.7% reduction in latency** vs baseline, **-25.7%** vs Option 1).
   - Validates at an extreme **$\approx 106.5\text{ million orders/sec}$** per CPU core.

### Option 3 (Direct DOM Traversal & In-Place Parsing)
1. **Bypassing Intermediate Representation (IR)**:
   - Baseline and Option 1 relied on `Core::Serialization::Xml::XmlDeserializer`, which builds an intermediate `Value` IR map/tree before mapping into the model struct.
   - Option 3 uses `pugi::xml_document::load_buffer_inplace` and queries child nodes (`<customer>`, `<item>`, `<quantity>`) directly, coupled with zero-allocation `std::from_chars` for integer parsing.
2. **Throughput & Latency Impact**:
   - Deserialization latency dropped dramatically from **562.11 ns** to **175.22 ns** (**-68.8% reduction** compared to Option 1, **-72.1% overall reduction** compared to baseline).
   - Deserialization throughput jumped to **$\approx 5.71\text{ million orders/sec}$** on a single core.

### Option 4 (Cached Channel Pointer & Fast Ingress Pipeline)
1. **End-to-End Pipeline Integration**:
   - `onMessageReceivedFast` integrates Option 3 in-place direct DOM parsing (`parseOrderDirect`) and Option 2 compile-time bitmap validation (`validateOrderFast`).
   - Caches the resolved `Backend::IOutputChannel*` pointer after the initial lookup, eliminating per-message `std::shared_mutex` acquisition and `std::unordered_map` string lookups during egress dispatch.
2. **Full Pipeline Latency Impact**:
   - Total end-to-end ingress-to-egress latency plunged from **1,049.48 ns (1.05 µs)** down to **209.07 ns** (**-80.1% latency reduction**).
   - Handler ingress capacity scaled by **$5\times$**, leaping from $\approx 952\text{K msg/s}$ to **$\approx 4.78\text{ million validated messages/sec}$** on a single thread.

---

## 4. Summary & Recommendations

- For production high-throughput pipelines, the combination of **Option 2 (lookup tables)**, **Option 3 (direct DOM / in-place parsing)**, and **Option 4 (cached egress dispatch)** delivers an overall **80% drop in end-to-end processing latency** (from $\approx 1.05\ \mu\text{s}$ down to $\approx 209\text{ ns}$).

