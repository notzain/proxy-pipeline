#include "common/memory/BufferPool.hpp"
#include <cassert>

namespace Common::Memory {

BufferPool::BufferPool(std::size_t blockSize, std::size_t blockCount)
    : m_blockSize(blockSize)
    , m_blockCount(blockCount)
    , m_storage(blockSize * blockCount)
    , m_nextFree(blockCount, InvalidIndex)
    , m_available(blockCount)
{
    if (blockCount > 0) {
        for (std::size_t i = 0; i + 1 < blockCount; ++i) {
            m_nextFree[i] = static_cast<std::uint32_t>(i + 1);
        }
        m_nextFree[blockCount - 1] = InvalidIndex;
        m_head.store(packHead(0, 0), std::memory_order_relaxed);
    }
}

BufferPool::~BufferPool() = default;

std::optional<BufferSlice> BufferPool::acquire()
{
    std::uint64_t head = m_head.load(std::memory_order_acquire);
    while (true) {
        const std::uint32_t idx = unpackIndex(head);
        if (idx == InvalidIndex) {
            return std::nullopt;
        }
        const std::uint32_t next = m_nextFree[idx];
        const std::uint64_t desired = packHead(unpackTag(head) + 1U, next);
        if (m_head.compare_exchange_weak(head, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            m_available.fetch_sub(1, std::memory_order_relaxed);
            std::byte* blockData = m_storage.data() + (static_cast<std::size_t>(idx) * m_blockSize);
            return BufferSlice(blockData, m_blockSize, static_cast<std::size_t>(idx), shared_from_this());
        }
    }
}

void BufferPool::release(std::size_t blockIndex) noexcept
{
    assert(blockIndex < m_blockCount);
    const auto idx = static_cast<std::uint32_t>(blockIndex);

    std::uint64_t head = m_head.load(std::memory_order_relaxed);
    while (true) {
        m_nextFree[idx] = unpackIndex(head);
        const std::uint64_t desired = packHead(unpackTag(head) + 1U, idx);
        if (m_head.compare_exchange_weak(head, desired, std::memory_order_release, std::memory_order_relaxed)) {
            m_available.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

std::size_t BufferPool::blockSize() const noexcept
{
    return m_blockSize;
}

std::size_t BufferPool::blockCount() const noexcept
{
    return m_blockCount;
}

std::size_t BufferPool::available() const noexcept
{
    return m_available.load(std::memory_order_relaxed);
}

} // namespace Common::Memory
