# Architectural Review: Message Ingress, Handler Processing & Egress Pipeline

This document captures the architectural review of the message pipeline—from `Backend::IInputChannel` ingestion, through `Core::Threading` orchestration and `Core::Handler::IHandler` execution, out to `Backend::IOutputChannel` transmission—and defines the concrete steps and solutions required to address each finding.

---

## Design Principles & Architectural Constraints

1. **High Performance Without "Magic" or Hacks**: Zero-copy and zero-allocation hot paths are prioritized, but must rely on explicit contracts rather than fragile heuristics, hidden coupling, or dead code paths.
2. **Protocol & Buffer Abstraction**: `Backend::MessageView` represents an in-flight message independent of how bytes are buffered (stack scratch, pooled `BufferSlice`, or SDK-owned memory) and independent of specific network protocols.
3. **Transparent Metadata Preservation**: Generic processing handlers operate on `message.payload()` without coupling to network headers (`UdpMetadata`, etc.), while external ingress/egress handlers (`UdpIngressHandler`, `UdpEgressHandler`) capture and reconstruct network headers seamlessly across any pipeline backend (`shm`, `uds`, `socket`, `file`, `inproc`, `vendor`).
4. **Homogeneous Backend Chains**: All handlers within a chain use the same active `Backend::IBackend` (handlers with different backends never talk to each other directly). External network boundaries are bridged into and out of the active backend solely via dedicated ingress/egress handlers (`UdpIngressHandler`, `UdpEgressHandler`).

---

## Findings & Actionable Solutions

### 1. Dead SPSC In-Place Code Path & 2 KB Truncation Heuristic in `ThreadPoolStrategy`

#### Problem
- In `core/src/threading/ThreadPoolStrategy.cpp`:
  - The constructor unconditionally allocates `m_sharedQueue` (`MpmcQueue<MessageTask, 4096>`) whenever `m_strictFifo == false` (lines 360–362).
  - In `pollUnconstrained()` (lines 136–169), `if (sharedQueue)` is always true and returns early.
  - Consequently, **Path B** (lines 171–259)—which attempts in-place 2 KB SPSC writes via `tryPushInPlace` and checks `if (inPlaceSlots[i].size == MessageTask::SmallBufferSize)` to guess whether `PosixSocketPoller` peeked an oversized packet—is **100% unreachable dead code**.
  - Furthermore, if Path B were ever reached with multiple input channels, multiple poller threads would concurrently push to the same single-producer `SpscQueue` worker rings (a data race), and the `size == 2048` check is a magic heuristic that misclassifies legitimate 2048-byte frames.
- Additionally, because `MessageTask` embeds a 2048-byte `m_inlineBuffer` primarily for that dead SPSC in-place path, every push and pop on `m_sharedQueue` (`MpmcQueue<MessageTask, 4096>`) moves a ~2.1 KB struct by value, even when the payload is already held zero-copy inside a `BufferSlice` or `Lease`.

#### Solution
1. **Remove Dead Path B & the 2 KB Peek Heuristic**:
   - Remove the unreachable SPSC in-place block and `jumboScratch` fallback from `pollUnconstrained()` in `core/src/threading/ThreadPoolStrategy.cpp` (and remove any corresponding `SmallBufferSize` peek assumptions in socket pollers).
   - Standardize `pollUnconstrained()` on the clean, zero-copy `IngressBatchScratch::preparePooled()` + `m_sharedQueue` path (which already writes directly into `BufferSlice` memory or transfers leased views in $O(1)$).
2. **Shrink `MessageTask` SBO for Cache-Friendly Queue Transfers**:
   - Reduce `MessageTask::SmallBufferSize` from `2048` to a cache-line-friendly size (e.g. `128` or `256` bytes) for tiny control frames, relying on `Common::Memory::BufferSlice` (from `IBackend::bufferPool()`) for normal/large frames and `m_leasedView` for SDK-owned payloads.
   - Remove `prepareInline()` and `commitInline()` from `core/include/core/threading/MessageTask.hpp` and `core/src/threading/MessageTask.cpp`.

#### Checklist
- [x] Delete unreachable SPSC in-place code and `jumboScratch` in `core/src/threading/ThreadPoolStrategy.cpp`.
- [x] Remove `MessageTask::prepareInline()` and `MessageTask::commitInline()` from `core/include/core/threading/MessageTask.hpp` and `core/src/threading/MessageTask.cpp`.
- [x] Reduce `MessageTask::SmallBufferSize` so `MpmcQueue` and `SpscQueue` slots remain compact and cache-friendly.

---

### 2. Redundant `MessageView` Array Copy & Lease Re-Attachment in `IHandler::processBatch()`

#### Problem
- `Core::Threading::Detail::dispatchReceivedBatch()` (`core/src/threading/MessageDispatcher.cpp`, lines 53–56) already attaches each `Backend::Lease` to its corresponding `Backend::MessageView` in `validBatch`.
- Immediately afterward, `IHandler::processBatch()` (`core/src/handler/IHandler.cpp`, lines 79–88) allocates a stack array `std::array<Backend::MessageView, 64> leasedMessages`, copies every `MessageView`, and calls `attachLease()` a second time (and would overflow if a batch ever exceeded 64 messages).

#### Solution
- Remove the duplicate `std::array<Backend::MessageView, 64> leasedMessages` copy loop in `IHandler::processBatch()` (`core/src/handler/IHandler.cpp`), since `MessageDispatcher` already binds each `MessageView` to its `Lease` before calling `handler.processBatch(channelName, messages, batchLeases)`.
- If `IHandler::processBatch()` can be called directly by unit tests with non-attached leases, attach leases in-place or ensure `MessageDispatcher` is the single place that binds `Lease` to `MessageView`.

#### Checklist
- [ ] Remove the redundant `leasedMessages` copy in `core/src/handler/IHandler.cpp`.
- [ ] Verify all unit tests calling `processBatch()` directly pass with `MessageView` lease binding.

---

### 3. Decouple `Core::Threading` and `MessageView` from `UdpMetadata` & Wire Format Details

#### Problem
1. **`ThreadPoolStrategy` Inspects UDP Headers Directly**:
   - `computeFlowWorkerIndex()` in `core/src/threading/ThreadPoolStrategy.cpp` (lines 24–50) includes `"backend/UdpMetadata.hpp"`, parses the raw packet via `Backend::MessageView::from(rawPacket)` on the poller thread (duplicating the parse done later by `MessageDispatcher` on the worker thread), and branches on `Backend::ProtocolId::Udp` to hash IPv4/IPv6 addresses and ports.
   - If the channel emits `PayloadFormat::Raw` (e.g. `VendorInputChannel`), `computeFlowWorkerIndex()` still attempts `MessageView::from(rawPacket)` on raw vendor bytes.
2. **`MessageView.hpp` Leaks Wire Format & Protocol Headers**:
   - `backend/interface/include/backend/MessageView.hpp` includes `EnvelopeHeader.hpp` and `UdpMetadata.hpp` and declares static wire-packing methods (`pack`, `packRaw`, `packUdp`, `packInto`, `withPayload`). Every generic handler that includes `MessageView.hpp` transitively pulls in UDP metadata structures and wire-packing APIs.
3. **Unaligned Metadata Cast in `MessageView::getMetadata<T>()`**:
   - `sizeof(EnvelopeHeader)` is 18 bytes, placing `m_metadata.data()` at a 2-byte aligned offset (`base + 18`), whereas `UdpMetadata` is `alignas(4)`. `reinterpret_cast<const T*>(m_metadata.data())` is undefined behavior on strict-alignment architectures and flagged by UBSan.

#### Solution
1. **Protocol-Agnostic Flow Hashing in `ThreadPoolStrategy`**:
   - Remove `#include "backend/UdpMetadata.hpp"` from `core/src/threading/ThreadPoolStrategy.cpp`.
   - Pass `Backend::PayloadFormat` into `computeFlowWorkerIndex()`. When `format == PayloadFormat::Enveloped`, hash `optView->metadata()` as an opaque `span<const std::byte>` (using FNV-1a over the metadata bytes + `channelIdx`) instead of branching on `ProtocolId::Udp` and `UdpMetadata` fields. When `format == PayloadFormat::Raw` (or metadata is empty), fall back to `channelIdx % numWorkers`.
   - In `UdpInputChannel::writeEnvelopePreamble()`, store only the deterministic flow-relevant fields or zero-initialize destination fields consistently so hashing `metadata()` pins a client flow (`srcIp`, `srcPort`, `dstIp`, `dstPort`) to the same worker thread.
2. **Enforce 8-Byte Alignment on `EnvelopeHeader` & Type-Safe `getMetadata<T>()`**:
   - Pad `EnvelopeHeader` from 18 bytes to 24 bytes (or use a safe unaligned load / `MetadataTraits<T>`) so `m_metadata.data()` is always 8-byte aligned and `getMetadata<T>()` verifies `m_protocol == MetadataTraits<T>::protocol`.
3. **Separate Wire Packing from `MessageView` (Optional / Clean Layering)**:
   - Keep `MessageView` focused on inspecting in-flight messages (`payload()`, `metadata()`, `protocol()`, `timestampNs()`, lease access) and move `UdpMetadata.hpp` inclusion out of `MessageView.hpp` (e.g. forward-declare `UdpMetadata` or move protocol-specific packers into protocol/codec headers).

#### Checklist
- [ ] Replace `UdpMetadata`-specific hashing in `core/src/threading/ThreadPoolStrategy.cpp` with protocol-agnostic metadata byte hashing and respect `PayloadFormat::Raw`.
- [ ] Remove `#include "backend/UdpMetadata.hpp"` from `core/src/threading/ThreadPoolStrategy.cpp` and `backend/interface/include/backend/MessageView.hpp`.
- [ ] Fix alignment of metadata following `EnvelopeHeader` (pad `EnvelopeHeader` to 24 bytes or use `MetadataTraits<T>` + alignment-safe access in `MessageView::getMetadata<T>()`).

---

### 4. Preserving Network Metadata Across Vendor Backend & Payload-Mutating Handlers

#### Problem
1. **`UdpIngressHandler` → `VendorBackend` → `UdpEgressHandler` Metadata Loss**:
   - Even when all handlers in a chain share `VendorBackend`, an external UDP pipeline (`UdpIngressHandler` → generic handler(s) → `UdpEgressHandler` running over `VendorBackend`) receives UDP packets via `Core::Net::UdpInputChannel` (which frames them with `EnvelopeHeader` + `UdpMetadata`) and forwards them into `VendorOutputChannel`.
   - Currently, `VendorOutputChannel::send(const MessageView&)` (`backend/vendor/src/VendorOutputChannel.cpp`, lines 67–73) only copies `message.payload()` into `Sdk::VendorMessage` and **discards `message.metadata()` and `message.protocol()`**!
   - When the downstream `VendorInputChannel` receives that `VendorMessage`, it delivers a raw payload with empty metadata (`PayloadFormat::Raw`), so `UdpEgressHandler` loses the originating client's `UdpMetadata` (`srcIp`/`srcPort`) and cannot `reply_to_origin`.
2. **Payload-Transforming Generic Handlers**:
   - If a generic processor handler modifies or re-serializes a payload and calls `MessageView::withPayload(view, newPayload)`, it allocates a `std::vector<std::byte>` on the heap containing a `PX` wire envelope.
   - Passing that `std::vector<std::byte>` to `m_outputChannel->send(span<const std::byte>)` bypasses `send(const MessageView&)`, losing any attached `Lease` context and forcing `VendorOutputChannel::send(span)` to strip the envelope again.

#### Solution
1. **Preserve Envelope Metadata in `VendorOutputChannel` / `VendorInputChannel` When Present**:
   - When `VendorOutputChannel::send(const MessageView&)` constructs a new `Sdk::VendorMessage` for a message that carries non-empty `metadata()` (or `protocol() != ProtocolId::Raw`, such as external ingress from `UdpIngressHandler`), or when `MessageView` is modified via `withPayload`, ensure metadata is preserved (either via SDK metadata fields if supported by the vendor SDK, or by preserving the envelope when `protocol != ProtocolId::Raw`).
   - Add a zero-allocation `MessageView::withPayload(span<const std::byte> newPayload) const noexcept` method on `MessageView` that returns a new `MessageView` referencing `newPayload` while retaining the original `m_metadata`, `m_protocol`, `m_timestampNs`, and `m_lease`!
   - Then `IOutputChannel::send(const MessageView&)` can check whether `message.rawBuffer()` is contiguous with `message.payload()`; if a handler rebound the payload via `message.withPayload(newPayload)`, the output channel packs `header + metadata + newPayload` on the fly (for buffer backends) or sends `newPayload` while settling/consuming the original `Lease` (for `VendorOutputChannel`).

#### Checklist
- [ ] Add a non-allocating `MessageView::withPayload(Common::Compat::span<const std::byte> newPayload) const noexcept` view-rebind method (or `IOutputChannel::sendPayload(const MessageView&, span<const std::byte>)`) so generic handlers can transform payloads without heap-allocating wire envelopes or dropping leases.
- [ ] Update `IOutputChannel::send(const MessageView&)` and `VendorOutputChannel::send(const MessageView&)` to handle rebound payload views cleanly.

---

### 5. Simplify `Backend::Lease` & Harden `SettlementToken` Type Safety

#### Problem
1. **Dual State Representation in `Backend::Lease`**:
   - `backend/interface/include/backend/Lease.hpp` maintains both `std::unique_ptr<ILeaseState> m_state` and `SettlementToken m_token`.
   - `SettlementToken` (`void* m_context`, `SettlerFn m_settler`, `DeleterFn m_deleter`) is strictly more general and zero-allocation, making `ILeaseState` dead weight that adds 8 bytes and extra branching to every `Lease` move, destruction, and settlement.
2. **Unchecked `void*` Cast in `VendorOutputChannel`**:
   - In `backend/vendor/src/VendorOutputChannel.cpp` (lines 51–54), `VendorOutputChannel::send(const MessageView&)` checks `if (auto* token = message.token())` and immediately casts `static_cast<VendorRelayNode*>(token->context())`.
   - While chains are homogeneous in production, verifying that the token actually originated from `VendorInputChannel` (e.g. by checking `token->settler() == &vendorSettler` or a static tag) prevents undefined behavior if a custom test or channel attaches a different `SettlementToken`.

#### Solution
1. **Unify `Lease` Around `SettlementToken`**:
   - Remove `ILeaseState.hpp`, `Lease::m_state`, `Lease::releaseState()`, `Lease::state()`, `MessageView::consumeLeaseState()`, and `MessageView::leaseState()`.
   - Keep `Lease` as a thin move-only wrapper around `SettlementToken` (or alias/merge them).
2. **Validate `SettlementToken` Origin Before Downcasting `context()`**:
   - Expose `SettlerFn SettlementToken::settler() const noexcept` (or a `const void* typeTag`) and verify `token->settler() == &VendorInputChannel::settleRelayNode` before casting `token->context()` to `VendorRelayNode*` in `VendorOutputChannel`.

#### Checklist
- [ ] Remove `ILeaseState` and consolidate `Backend::Lease` to use `SettlementToken` exclusively.
- [ ] Add settler/tag verification in `VendorOutputChannel::send(const MessageView&)` before casting `token->context()` to `VendorRelayNode*`.

---

### 6. Vectorized Batch Egress in `UdpEgressHandler`

#### Problem
- `UdpEgressHandler::onBatchReceived()` (`handler/udp_egress_handler/src/UdpEgressHandler.cpp`, lines 85–94) loops over `onMessageReceived()` one packet at a time, invoking one `::sendto()` syscall per packet even in `forward` mode where all packets target the same destination `(m_destHost, m_destPort)`.
- Furthermore, `Core::Net::UdpOutputChannel::sendBatch()` (`core/src/net/UdpOutputChannel.cpp`, lines 110–119) loops over `send()` instead of using `::sendmmsg()` on Linux or a connected UDP socket.

#### Solution
- Update `UdpEgressHandler::onBatchReceived()` to use `m_udpOut->sendBatch()` when in `forward` mode (extracting payload spans into a `SmallSpanBuffer`).
- Implement `::sendmmsg()` batching in `Core::Net::UdpOutputChannel::sendBatch()` (matching `SocketOutputChannel::sendBatch()`).

#### Checklist
- [ ] Vectorize `UdpEgressHandler::onBatchReceived()` in `forward` mode using `SmallSpanBuffer` and `m_udpOut->sendBatch()`.
- [ ] Implement `::sendmmsg()` on Linux in `Core::Net::UdpOutputChannel::sendBatch()`.

