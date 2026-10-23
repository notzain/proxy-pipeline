#include "core/memory/BufferPool.hpp"
#include <thread>

namespace Core::Memory {

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
        std::size_t currentTop = m_freeTop.load(std::memory_order_relaxed);
        if (currentTop == 0) {
            return std::nullopt; // Pool exhausted
        }

        std::size_t expected = 0;
        if (!m_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            std::this_thread::yield();
            continue;
        }

        currentTop = m_freeTop.load(std::memory_order_relaxed);
        if (currentTop == 0) {
            m_lock.store(0, std::memory_order_release);
            return std::nullopt;
        }

        std::size_t index = m_freeIndices[currentTop - 1];
        m_freeTop.store(currentTop - 1, std::memory_order_relaxed);
        m_lock.store(0, std::memory_order_release);

        std::byte* ptr = m_storage.data() + (index * m_blockSize);
        return BufferSlice(ptr, m_blockSize, index, shared_from_this());
    }
}

void BufferPool::release(std::size_t blockIndex) noexcept
{
    while (true) {
        std::size_t expected = 0;
        if (m_lock.compare_exchange_weak(expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            std::size_t currentTop = m_freeTop.load(std::memory_order_relaxed);
            if (currentTop < m_blockCount) {
                m_freeIndices[currentTop] = blockIndex;
                m_freeTop.store(currentTop + 1, std::memory_order_relaxed);
            }
            m_lock.store(0, std::memory_order_release);
            return;
        }
        std::this_thread::yield();
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
    return m_freeTop.load(std::memory_order_relaxed);
}

} // namespace Core::Memory
