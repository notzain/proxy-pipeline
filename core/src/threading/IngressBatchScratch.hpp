#pragma once

#include "backend/BufferSlot.hpp"
#include "backend/IInputChannel.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/BufferPool.hpp"
#include "common/memory/BufferSlice.hpp"
#include "common/memory/MutableByteView.hpp"
#include <array>
#include <cstddef>
#include <memory>
#include <optional>

namespace Core::Threading::Detail {

/**
 * @brief Reusable batch ingress scratch manager that adapts to a channel's memory model.
 *
 * - For channels where `providesOwnMemory() == true` (e.g. `VendorInputChannel`):
 *   allocates zero scratch memory and leaves `slot.buffer` empty.
 * - For synchronous strategies (`SequentialStrategy`, `DedicatedThreadStrategy`, `SimpleExecutionStrategy`):
 *   lazily allocates a contiguous per-thread scratch array on first poll of a normal channel, keeping
 *   buffers L2/L3-hot with zero atomic operations.
 * - For asynchronous worker handoff (`ThreadPoolStrategy` strict-FIFO and MPMC shared queue):
 *   pre-attaches `BufferSlice`s acquired directly from `IBackend::bufferPool()` into `slot.buffer`
 *   so the kernel (`recvmmsg` / `read`) writes straight into pool memory, eliminating a 64 KB `memcpy`
 *   per packet when constructing `MessageTask`s. Falls back to the per-thread scratch buffer only if
 *   the pool is temporarily exhausted.
 */
template <std::size_t MaxBatch = 16, std::size_t SlotBytes = 65535>
class IngressBatchScratch {
public:
    using ScratchArray = std::array<std::array<std::byte, SlotBytes>, MaxBatch>;

    /**
     * @brief Prepares slots for synchronous in-place consumption (reuses L2/L3-hot scratch buffers).
     */
    Common::Compat::span<::Backend::BufferSlot> prepareSync(const ::Backend::IInputChannel& channel)
    {
        const bool ownMemory = channel.providesOwnMemory();
        if (!ownMemory && !m_scratch) {
            m_scratch = std::make_unique<ScratchArray>();
        }
        for (std::size_t i = 0; i < MaxBatch; ++i) {
            m_slots[i].reset();
            m_slices[i].reset();
            m_slots[i].buffer = ownMemory
                ? Common::Compat::span<std::byte> { }
                : Common::Memory::MutableByteView((*m_scratch)[i]);
        }
        return Common::Compat::span<::Backend::BufferSlot>(m_slots.data(), MaxBatch);
    }

    /**
     * @brief Prepares slots for zero-copy `BufferSlice` handoff to worker queues.
     *
     * Acquires a `BufferSlice` per slot directly from @p pool (when its blockSize >= SlotBytes) so the
     * transport writes straight into the slice without an intermediate `batchBuffers` copy.
     */
    Common::Compat::span<::Backend::BufferSlot> preparePooled(
        const ::Backend::IInputChannel& channel,
        const std::shared_ptr<Common::Memory::BufferPool>& pool)
    {
        const bool ownMemory = channel.providesOwnMemory();
        const bool canDirectPool = !ownMemory && pool && pool->blockSize() >= SlotBytes;

        for (std::size_t i = 0; i < MaxBatch; ++i) {
            m_slots[i].reset();
            m_slices[i].reset();
            if (ownMemory) {
                m_slots[i].buffer = { };
                continue;
            }
            if (canDirectPool) {
                if (auto slice = pool->acquire()) {
                    m_slots[i].buffer = slice->span();
                    m_slices[i] = std::move(*slice);
                    continue;
                }
            }
            if (!m_scratch) {
                m_scratch = std::make_unique<ScratchArray>();
            }
            m_slots[i].buffer = Common::Memory::MutableByteView((*m_scratch)[i]);
        }
        return Common::Compat::span<::Backend::BufferSlot>(m_slots.data(), MaxBatch);
    }

    /**
     * @brief Releases any unused pre-acquired `BufferSlice`s beyond @p receivedCount immediately after `receiveBatch()`.
     */
    void releaseUnusedFrom(std::size_t receivedCount) noexcept
    {
        for (std::size_t i = receivedCount; i < MaxBatch; ++i) {
            m_slices[i].reset();
        }
    }

    [[nodiscard]] ::Backend::BufferSlot& slot(std::size_t i) noexcept
    {
        return m_slots[i];
    }

    [[nodiscard]] std::optional<Common::Memory::BufferSlice>& slice(std::size_t i) noexcept
    {
        return m_slices[i];
    }

private:
    std::array<::Backend::BufferSlot, MaxBatch> m_slots { };
    std::array<std::optional<Common::Memory::BufferSlice>, MaxBatch> m_slices { };
    std::unique_ptr<ScratchArray> m_scratch;
};

} // namespace Core::Threading::Detail
