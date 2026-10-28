# Message Envelope Wire Format Specification & Backend Decoupling

## Overview

The Proxy framework uses message envelopes to frame data across IPC channels, network interfaces, and handler execution pipelines.

Historically:
1. `EnvelopeHeader` was declared as a compiler-packed C++ struct (`#pragma pack(push, 1)`) and copied directly to/from memory via `std::memcpy` or pointer cast (`reinterpret_cast<const EnvelopeHeader*>`). This coupled the wire representation to host byte order and compiler packing behaviour.
2. In-flight messages were represented by `Backend::MessageView` which assumed that all incoming messages begin with `EnvelopeHeader` (`EnvelopeMagic = 0x5058`).
3. Future requirements dictate that:
   - The native Proxy envelope wire format must be specified with explicit endianness (network byte order / big-endian) and explicit field-by-field serialization/deserialization.
   - Handlers should consume messages through an interface that strictly exposes the application `payload` (plus transport attributes like `rawBuffer` and timestamps), without assuming every backend uses the Proxy envelope header.
   - Future backends may use their own proprietary protocol framing (magic headers, framing bytes, protocol specifications), while handlers only interact with the decoded application payload.

---

## Design Decisions

### 1. Wire Format Specification (Proxy Envelope v1)

The Proxy Envelope v1 wire layout consists of an 18-byte fixed header followed by optional metadata and the payload:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Magic (0x5058)       |    Version    |  Protocol ID  |
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

#### Field Definitions:
- **Magic (`uint16_t`)**: Fixed value `0x5058` (`'P'`, `'X'`). Big-endian wire representation: byte 0 = `0x50`, byte 1 = `0x58`.
- **Version (`uint8_t`)**: Envelope format version. Current version is `1`.
- **Protocol ID (`uint8_t`)**: Identifies transport metadata semantics:
  - `0`: Raw (no metadata)
  - `1`: UDP (metadata matches `UdpMetadata`)
- **Metadata Length (`uint16_t`)**: Length of the transport metadata segment in bytes, encoded in network byte order (big-endian).
- **Payload Length (`uint32_t`)**: Length of the application payload segment in bytes, encoded in network byte order (big-endian).
- **Timestamp (`uint64_t`)**: Ingress UNIX epoch timestamp in nanoseconds, encoded in network byte order (big-endian).
- **Metadata (`byte[metadataLength]`)**: Protocol-specific metadata bytes.
- **Payload (`byte[payloadLength]`)**: Application data payload.

Total frame size: `18 + metadataLength + payloadLength`.

### 2. Explicit Serialization & Parsing

- Encoding functions serialize fields using bitwise shifting into destination byte spans.
- Decoding functions extract 16/32/64-bit integers explicitly byte-by-byte, eliminating all undefined behavior from unaligned pointers or endian differences.
- Update `scripts/proxy_runner/pcap.py` to write/read big-endian network byte order (`!HBBHIQ`).

### 3. Backend & Handler Decoupling: Payload-First Abstraction

Handlers only care about application data:
- `MessageView` represents a view of a message. It provides:
  - `payload()`: `std::span<const std::byte>` (primary API exposed to handler logic).
  - `rawBuffer()`: `std::span<const std::byte>` (the full buffer for forwarding over channels).
  - `timestampNs()`: Ingress timestamp (or 0 if not provided).
  - `protocol()` / `metadata()`: Protocol attributes for routing or egress handlers.
- `MessageView` can be constructed directly from:
  1. A standard Proxy envelope buffer (`MessageView::from(buffer)`).
  2. A custom backend frame via constructor `MessageView(payload, rawBuffer, timestampNs, protocol, metadata)`.
- If a future backend uses a proprietary protocol (custom magic bytes, frame header, length tags), that backend or channel decoder can extract the payload and construct a `MessageView` pointing to the payload without forcing the wire format to match `0x5058`.
- In `MessageDispatcher`, allow non-envelope or fallback raw payloads (or protocol decoding hooks) instead of dropping non-0x5058 frames when configured.

---

## Tasks / Checklist

- [x] **1. Create Wire Encoding/Decoding Primitives**
  - Implement portable big-endian integer read/write functions for 16-bit, 32-bit, and 64-bit unsigned integers.
- [x] **2. Update `EnvelopeHeader`, `MessageEnvelope`, and `MessageView`**
  - Update `MessageEnvelope::packInto` to write fields in network byte order (big-endian).
  - Update `MessageView::from` to safely parse fields in network byte order without pointer reinterpretation.
  - Add generalized `MessageView` constructor allowing external/custom backends to create views directly from payload and raw spans.
  - Keep `payload()` as the central, uniform accessor for handlers.
- [x] **3. Update Python Tooling (`scripts/proxy_runner/pcap.py`)**
  - Update struct packing in `pcap.py` to use big-endian format `!HBBHIQ` matching the explicit wire specification.
- [x] **4. Test Vectors & Unit Tests**
  - Add unit test with hardcoded golden byte sequences verifying exact byte-by-byte layout, byte orders, and boundary checks.
  - Verify all existing tests, handlers, and benchmarks pass cleanly.

