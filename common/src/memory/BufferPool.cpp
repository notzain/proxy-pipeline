#include "common/memory/BufferPool.hpp"
#include <cassert>

namespace Common::Memory {

BufferPool::BufferPool(std::size_t blockSize, std::size_t blockCount)
    : m_blockSize(blockSize)
    , m_blockCount(blockCount)
    , m_storage(blockSize * blockCount)
    , m_freeIndices(blockCount)
    , m_freeTop(blockCount)
{
    for (std::size_t i = 0; i < blockCount; ++i) {
        m_freeIndices[i] = i;
    }
}

BufferPool::~BufferPool() = default;

std::optional<BufferSlice> BufferPool::acquire()
{
    while (true) {
        std::size_t expected = 0;
        if (m_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            break;
        }
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#endif
    }

    std::size_t top = m_freeTop.load(std::memory_order_relaxed);
    if (top == 0) {
        m_lock.store(0, std::memory_order_release);
        return std::nullopt;
    }

    std::size_t blockIdx = m_freeIndices[top - 1];
    m_freeTop.store(top - 1, std::memory_order_relaxed);

    m_lock.store(0, std::memory_order_release);

    std::byte* blockData = m_storage.data() + (blockIdx * m_blockSize);
    return BufferSlice(blockData, m_blockSize, blockIdx, shared_from_this());
}

void BufferPool::release(std::size_t blockIndex) noexcept
{
    assert(blockIndex < m_blockCount);

    while (true) {
        std::size_t expected = 0;
        if (m_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            break;
        }
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#endif
    }

    std::size_t top = m_freeTop.load(std::memory_order_relaxed);
    assert(top < m_blockCount);
    m_freeIndices[top] = blockIndex;
    m_freeTop.store(top + 1, std::memory_order_relaxed);

    m_lock.store(0, std::memory_order_release);
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
    return m_freeTop.load(std::memory_order_relaxed);
}

} // namespace Common::Memory
