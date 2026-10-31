# Architectural & Implementation TODOs (`MessageView` & Wire Envelope Pipeline)

Tracked follow-up improvements and architectural findings for `Backend::MessageView`, wire framing, and message lifecycle management.

---

## 1. Fix Unaligned Memory Access UB in `MessageView::getMetadata<T>()`
- **Status**: **RESOLVED** (Commit `4f8b2b8`, Feature `015`)
- **Resolution**: `MessageView::getMetadata<T>()` returns `std::optional<T>` by value using `std::memcpy(&out, m_metadata.data(), sizeof(T))` rather than `reinterpret_cast<const T*>`. It enforces `static_assert(std::is_trivially_copyable_v<T>)`, eliminating undefined behavior from unaligned accesses.
- **Tasks**:
  - [x] Return `std::optional<T>` by value via `std::memcpy` in `MessageView::getMetadata<T>()`.
  - [x] Enforce compile-time trivially copyable constraints on metadata types.

## 2. Enforce Protocol Type-Safety in `MessageView::getMetadata<T>()`
- **Status**: **RESOLVED** (Commit `4f8b2b8`, Feature `015`)
- **Resolution**: Introduced `Backend::MetadataTraits<T>` extension point and replaced the closed `ProtocolId` enum with `Common::Types::StrongType<std::uint8_t, ProtocolIdTag>`. `MessageView::getMetadata<T>()` explicitly asserts `MetadataTraits<T>::isRegistered` and verifies `m_protocol == MetadataTraits<T>::protocol && m_metadata.size() >= sizeof(T)`.
- **Tasks**:
  - [x] Introduce `MetadataTraits<T>` specialization mapping each metadata struct (e.g. `UdpMetadata -> ProtocolId::Udp` / `17`) to its authoritative `ProtocolId`.
  - [x] Update `MessageView::getMetadata<T>()` to verify `m_protocol == MetadataTraits<T>::protocol` and validate metadata length.

## 3. Deduplicate Manual Binary Header Offsets Across Channels
- **Status**: **RESOLVED** (Feature `016`)
- **Resolution**: Added authoritative `Backend::writeEnvelopeHeader()` in `backend/interface/include/backend/EnvelopeHeader.hpp` and `backend/interface/src/EnvelopeHeader.cpp`. Replaced duplicate preamble serialization in `MessageView::packInto()` and `Core::Net::UdpInputChannel::writeEnvelopePreamble()`. Replaced manual header offset unpacks in `SimInputChannel::emitEvent()`, `SimOutputChannel::emitEvent()`, and `SimBackend::recordTrace()` with `MessageView::from()`.
- **Tasks**:
  - [x] Add an authoritative `writeEnvelopeHeader` helper in `backend/interface` and use it in `MessageView::packInto()` and `UdpInputChannel::writeEnvelopePreamble()`.
  - [x] Replace manual header byte parsing in `SimInputChannel::emitEvent()`, `SimOutputChannel::emitEvent()`, and `SimBackend::recordTrace()` with `MessageView::from()`.
  - [x] Add unit test coverage for `writeEnvelopeHeader`.

## 4. Clarify Wire Envelope vs. External Boundary Framing
- **Status**: **RESOLVED** (Commits `46fcc61`, `66f676a`, `cb1141d`, Feature `015`)
- **Resolution**:
  - Added `Backend::PayloadFormat` enum (`PayloadFormat::Enveloped` vs `PayloadFormat::Raw`) to `BufferSlot`. Boundary channels (e.g. `VendorInputChannel`) emit `PayloadFormat::Raw`, while standard channels emit `PayloadFormat::Enveloped`.
  - `MessageDispatcher::dispatchReceivedBatch()` checks `slot.format == PayloadFormat::Raw` and instantiates `MessageView(rawMessage)` directly without running envelope validation, completely preventing boundary drops or misclassifications.
  - Added `MessageView::withPayload()` and `MessageView::hasWireEnvelope()`. `IOutputChannel::send(const MessageView&)` automatically detects whether a view carries a wire envelope; if raw or rebound, it frames the wire envelope on the stack (when `<= 4096` bytes) or transmits directly.
- **Tasks**:
  - [x] Unify boundary vs enveloped ingress representation via `BufferSlot::format` and `PayloadFormat::Raw`.
  - [x] Add `MessageView::hasWireEnvelope()` and stack wire framing in `IOutputChannel::send(const MessageView&)`.
  - [x] Support zero-allocation consuming forwards and transparent payload mutation in `VendorOutputChannel`.

## 5. Add Zero-Allocation `MessageEnvelope::withPayloadInto()`
- **Status**: **RESOLVED** (Commit `66f676a`, Feature `015`)
- **Resolution**:
  - `MessageEnvelope` class was eliminated and unified into `MessageView`.
  - Added non-allocating `MessageView::withPayload(span<const std::byte> newPayload) const noexcept`, which returns a rebound `MessageView` preserving protocol, metadata, timestamp, and `Lease` without any heap allocations.
  - Added generic `MessageView::pack<T>()` and zero-allocation `MessageView::packInto<T>()` templates in `backend/interface/include/backend/MessageView.hpp`.
- **Tasks**:
  - [x] Implement non-allocating `MessageView::withPayload()`.
  - [x] Implement zero-allocation `MessageView::packInto()` and `MessageView::packInto<T>()`.

## 6. Separate Declarations and Implementations in `MessageView`
- **Status**: **RESOLVED** (Commit `1dca32c`)
- **Resolution**: All non-templated constructors and member functions (`payload()`, `rawBuffer()`, `metadata()`, `protocol()`, `timestampNs()`, `attachLease()`, `token()`, `hasWireEnvelope()`, `withPayload()`, `pack()`, `packRaw()`, `packInto()`, `from()`) are declared in `backend/interface/include/backend/MessageView.hpp` and defined in `backend/interface/src/MessageView.cpp` in compliance with `AGENTS.md` Section 6. Only templated helpers (`pack<T>`, `packInto<T>`, `getMetadata<T>`) remain in the header.
- **Tasks**:
  - [x] Move all non-templated constructors and member methods from `backend/interface/include/backend/MessageView.hpp` into `backend/interface/src/MessageView.cpp`.

## 7. Review Clock Domain for `timestampNs` Telemetry
- **Status**: **VALID**
- **Problem**: `MessageView::packInto()` and `Core::Net::UdpInputChannel` use `std::chrono::system_clock::now()` (`getCurrentTimeNs()`), which is non-monotonic under NTP adjustments and can skew intra-host hop latency histograms measured in `IHandler::processBatch()` (`nowNs - message.timestampNs()`).
- **Tasks**:
  - [ ] Standardize intra-host pipeline timestamps on `std::chrono::steady_clock` (or a high-resolution monotonic clock like `clock_gettime(CLOCK_MONOTONIC)`), reserving `system_clock` (wall-clock / epoch) strictly for cross-host distributed tracing protocols where synchronized wall-clock timestamps are explicitly required.
  - [ ] Update `MessageView::packInto()`, `UdpInputChannel`, and `IHandler::processBatch()` to use a consistent monotonic timebase for hop duration metrics.
