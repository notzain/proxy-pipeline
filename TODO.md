# Architectural & Implementation TODOs (`MessageEnvelope` & `MessageView`)

Tracked follow-up improvements identified during the `Backend::MessageEnvelope` and `Backend::MessageView` architecture review.

---

## 1. Fix Unaligned Memory Access UB in `MessageView::getMetadata<T>()`
- **Problem**: `EnvelopeHeader` is 18 bytes (`sizeof(EnvelopeHeader) == 18`), so `m_metadata.data()` (`buffer.data() + 18`) is only 2-byte aligned even when `buffer.data()` is 8- or 16-byte aligned. Calling `reinterpret_cast<const T*>(m_metadata.data())` in `MessageView::getMetadata<T>()` for types with `alignof(T) > 2` (such as `UdpMetadata`, which is `alignas(4)`, or any 8-byte aligned metadata struct) is undefined behavior under C++ alignment rules.
- **Tasks**:
  - [ ] Pad `EnvelopeHeader` to 24 bytes (8-byte multiple) in `backend/interface/include/backend/EnvelopeHeader.hpp` or return `std::optional<T>` by value via `std::memcpy` in `MessageView::getMetadata<T>()`.
  - [ ] Add `static_assert` alignment checks in `MessageView::getMetadata<T>()`.

## 2. Enforce Protocol Type-Safety in `MessageView::getMetadata<T>()`
- **Problem**: `MessageView::getMetadata<T>()` only checks `m_metadata.size() >= sizeof(T)` without verifying that `m_protocol` matches the expected `ProtocolId` for `T`.
- **Tasks**:
  - [ ] Introduce a `MetadataTraits<T>` specialization mapping each metadata struct (e.g. `UdpMetadata -> ProtocolId::Udp`) to its authoritative `ProtocolId`.
  - [ ] Update `MessageView::getMetadata<T>()` to verify `m_protocol == MetadataTraits<T>::protocol && m_metadata.size() == sizeof(T)`.

## 3. Deduplicate Manual Binary Header Offsets Across Channels
- **Problem**: `EnvelopeHeader` is declared as a `#pragma pack(push, 1)` struct, but raw byte offsets (`dst + 4`, `dst + 6`, `dst + 10`) are manually duplicated across:
  - `backend/interface/src/MessageEnvelope.cpp` (`MessageEnvelope::packInto()`)
  - `backend/interface/src/MessageView.cpp` (`MessageView::from()`)
  - `core/src/net/UdpInputChannel.cpp` (`writeEnvelopePreamble()`)
  - `backend/sim/src/SimInputChannel.cpp` (`SimInputChannel::emitEvent()`)
- **Tasks**:
  - [ ] Add a single authoritative `MessageEnvelope::writeHeader()` helper in `backend/interface` and use it in both `MessageEnvelope::packInto()` and `UdpInputChannel`.
  - [ ] Replace manual header byte parsing in `SimInputChannel::emitEvent()` with `MessageView::from()`.

## 4. Clarify Wire Envelope vs. External Boundary Framing
- **Problem**: `MessageDispatcher::dispatchReceivedBatch()` requires every `IInputChannel` to emit `0x5058` (`PX`) enveloped frames, forcing external boundary channels (`VendorInputChannel` / `VendorOutputChannel`) to wrap payloads into a `PX` envelope on ingress and strip the `PX` envelope on egress. Meanwhile, `MessageView` exposes a public constructor taking a raw un-enveloped payload where `rawBuffer()` is not a valid `PX` envelope, which breaks downstream forwarding if used.
- **Tasks**:
  - [ ] Remove or restrict the raw-payload `MessageView(span<const byte> payload, ...)` constructor in `backend/interface/include/backend/MessageView.hpp` so `MessageView::rawBuffer()` is always guaranteed to be a valid `PX` envelope.
  - [ ] Evaluate whether boundary channels should frame via `MessageEnvelope::packInto()` or whether `BufferSlot` / `MessageDispatcher` should support raw boundary payloads natively.

## 5. Add Zero-Allocation `MessageEnvelope::withPayloadInto()`
- **Problem**: `MessageEnvelope::withPayload()`, `pack()`, `packRaw()`, and `packUdp()` allocate a `std::vector<std::byte>` on every call, whereas `packInto()` writes into a caller-provided span without heap allocation.
- **Tasks**:
  - [ ] Add `MessageEnvelope::withPayloadInto(Common::Compat::span<std::byte> destination, const MessageView& view, Common::Compat::span<const std::byte> newPayload)` in `backend/interface/include/backend/MessageEnvelope.hpp` and `backend/interface/src/MessageEnvelope.cpp`.
  - [ ] Add `MessageEnvelope::packRawInto()` and `MessageEnvelope::packUdpInto()` convenience helpers.

## 6. Separate Declarations and Implementations in `MessageView`
- **Problem**: `backend/interface/include/backend/MessageView.hpp` defines non-templated constructors and getters inline in the header, violating the project's declaration/implementation separation rule (`AGENTS.md` Section 6).
- **Tasks**:
  - [ ] Move all non-templated constructors and member functions (`payload()`, `rawBuffer()`, `metadata()`, `protocol()`, `timestampNs()`) from `backend/interface/include/backend/MessageView.hpp` into `backend/interface/src/MessageView.cpp`.

## 7. Review Clock Domain for `timestampNs` Telemetry
- **Problem**: `MessageEnvelope` and `IHandler::processBatch()` use `std::chrono::system_clock::now()`, which is non-monotonic under NTP adjustments and can skew intra-host hop latency histograms.
- **Tasks**:
  - [ ] Evaluate using `std::chrono::steady_clock` for intra-host timestamps or documenting wall-clock requirements for cross-host UDP/socket telemetry.

