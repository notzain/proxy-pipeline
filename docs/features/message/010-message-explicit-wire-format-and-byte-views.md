# 010: Explicit Big-Endian Wire Format v1 & `ByteView` Abstractions

## 1. Overview & Problem Statement

### Why It's Needed
In `1fecaf9` – `12bcf75`, `EnvelopeHeader` was previously defined as a `#pragma pack(push, 1)` C++ struct read via `reinterpret_cast<const EnvelopeHeader*>(buffer.data())`. This created three portability and architectural problems:
1. **Host Endianness & Alignment Undefined Behavior**: Casting unaligned byte pointers to packed structs is undefined behavior in C++ and tied the wire format to little-endian CPU architectures.
2. **Verbose Span Conversions**: Converting `std::string`, `std::string_view`, `std::vector<std::byte>`, and `std::array<std::byte, N>` into `Common::Compat::span<const std::byte>` required repetitive `reinterpret_cast` boilerplate across handlers and tests.
3. **Cross-Language Tooling Compatibility**: Python PCAP generators (`scripts/proxy_runner/pcap.py`) needed a strict, byte-exact specification to synthesize and inspect frames.

### The Solution
1. **Portable Big-Endian Wire Serialization (`Common::Io::WireFormat`)**: Defined Proxy Envelope v1 as an explicit 18-byte big-endian (network byte order) header serialized and parsed field-by-field (`common/include/common/io/WireFormat.hpp`).
2. **`ByteView` & `MutableByteView`**: Introduced zero-cost view wrappers in `common/include/common/memory/` that implicitly construct from contiguous byte/char containers (`std::string`, `std::string_view`, `std::vector`, `std::array`, `span`) and convert directly to `Common::Compat::span`.

---

## 2. Architecture & How It Works

### 2.1 Proxy Envelope v1 Wire Layout (18-Byte Big-Endian Header)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Magic (0x5058)       |  Version (1)  |  Protocol ID  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|        Metadata Length        |         Payload Length        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       (Payload Length)        |                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                    Timestamp (nanoseconds)                    |
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |     Metadata (variable) ...   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|      ...                      |     Payload (variable) ...    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Size | Field | Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0..1` | 2 B | `magic` | `uint16_t` | `0x5058` (`'P'`, `'X'`) in big-endian (`0x50`, `0x58`) |
| `2` | 1 B | `version` | `uint8_t` | Format version (`1`) |
| `3` | 1 B | `protocol` | `uint8_t` | `ProtocolId` (`0` = Raw, `1` = UDP) |
| `4..5` | 2 B | `metadataLength` | `uint16_t` | Big-endian length of metadata section in bytes |
| `6..9` | 4 B | `payloadLength` | `uint32_t` | Big-endian length of payload section in bytes |
| `10..17` | 8 B | `timestampNs` | `uint64_t` | Big-endian UNIX epoch ingress timestamp in nanoseconds |
| `18..` | Var | `metadata` | `byte[]` | Optional protocol metadata (e.g. `UdpMetadata`) |
| `18+M..` | Var | `payload` | `byte[]` | Application payload bytes |

---

## 3. Design Decisions

### 3.1 Bitwise Big-Endian Encoding (`Common::Io::WireFormat`)
- **Decision**: Encode and decode all multi-byte header integers via explicit shift/mask helpers (`writeBe16`, `writeBe32`, `writeBe64`, `readBe16`, `readBe32`, `readBe64`).
- **Rationale**: Compilers optimize these into single `bswap` / `rev` instructions on little-endian CPUs while guaranteeing zero undefined behavior regardless of buffer alignment.

### 3.2 `ByteView` and `MutableByteView`
- **Decision**: Provide `Common::Memory::ByteView` (const bytes) and `Common::Memory::MutableByteView` (mutable bytes) in `common/include/common/memory/`.
- **Rationale**: Eliminates `reinterpret_cast<const std::byte*>` clutter when packing strings or buffers into `MessageEnvelope::packInto()` and `IOutputChannel::send()`.

---

## 4. Concrete Examples

### 4.1 Packing and Parsing with `ByteView`
```cpp
std::string payload = "<order><customer>Alice</customer></order>";
auto envelope = Backend::MessageEnvelope::packRaw(Common::Memory::ByteView(payload));

if (auto view = Backend::MessageView::from(Common::Memory::ByteView(envelope))) {
    // view->payload() points directly to the 41-byte XML payload slice
}
```

---

## 5. Tasks / Checklist

- [x] Implement big-endian read/write helpers in `common/include/common/io/WireFormat.hpp`.
- [x] Refactor `EnvelopeHeader`, `MessageEnvelope::packInto`, and `MessageView::from` to use explicit 18-byte big-endian wire serialization.
- [x] Implement `View`, `ByteView`, and `MutableByteView` in `common/include/common/memory/`.
- [x] Add golden byte-vector unit tests verifying exact big-endian layout.

