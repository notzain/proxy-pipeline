#pragma once

#include "common/compat/StopToken.hpp"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace Common::Queue {

namespace Detail {

[[nodiscard]] constexpr std::size_t nextPowerOfTwo(std::size_t v) noexcept
{
    if (v <= 2) {
        return 2;
    }
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}

} // namespace Detail

/**
 * @brief High-performance lock-free Multi-Producer Multi-Consumer (MPMC) bounded ring buffer
 * with runtime-configured capacity.
 *
 * Implements Dmitry Vyukov's array-based bounded MPMC queue algorithm with dynamically
 * allocated storage sized at construction time. The requested capacity is automatically
 * rounded up to the nearest power of two.
 *
 * Cache-line alignment (alignas(64)) is applied to the head and tail position counters and
 * individual queue cells to eliminate false sharing between concurrent push and pop threads.
 *
 * @tparam T Element type stored in the queue. Must be default constructible and movable.
 */
template <typename T>
class BoundedMpmcQueue {
public:
    explicit BoundedMpmcQueue(std::size_t requestedCapacity = 1024)
        : m_capacity(Detail::nextPowerOfTwo(requestedCapacity))
        , m_mask(m_capacity - 1)
        , m_cells(std::make_unique<Cell[]>(m_capacity))
    {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            m_cells[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~BoundedMpmcQueue()
    {
        stop();
    }

    BoundedMpmcQueue(const BoundedMpmcQueue&) = delete;
    BoundedMpmcQueue& operator=(const BoundedMpmcQueue&) = delete;
    BoundedMpmcQueue(BoundedMpmcQueue&&) = delete;
    BoundedMpmcQueue& operator=(BoundedMpmcQueue&&) = delete;

    /**
     * @brief Attempts to push an item into the queue without blocking.
     *
     * @tparam U Type convertible to T.
     * @param data The item to push.
     * @return true if successfully enqueued, false if queue was full or contended.
     */
    template <typename U>
    bool tryPush(U&& data)
    {
        std::size_t pos = m_tail.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = m_cells[pos & m_mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (m_tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = std::forward<U>(data);
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_tail.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Pushes an item into the queue, blocking with cooperative yield/spin until space is available.
     *
     * @tparam U Type convertible to T.
     * @param data Item to enqueue.
     * @param stoken Cooperative cancellation token.
     * @return true if successfully pushed, false if stop was requested.
     */
    template <typename U>
    bool push(U&& data, Common::Compat::stop_token stoken = { })
    {
        std::size_t pos = m_tail.load(std::memory_order_relaxed);
        while (!stoken.stop_requested() && !m_stopped.load(std::memory_order_relaxed)) {
            Cell& cell = m_cells[pos & m_mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (m_tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = std::forward<U>(data);
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                std::this_thread::yield();
                pos = m_tail.load(std::memory_order_relaxed);
            } else {
                pos = m_tail.load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    /**
     * @brief Attempts to pop an item from the queue without blocking.
     *
     * @param out Reference receiving the popped element.
     * @return true if an element was popped, false if queue was empty or contended.
     */
    bool tryPop(T& out)
    {
        std::size_t pos = m_head.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = m_cells[pos & m_mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(cell.data);
                    cell.sequence.store(pos + m_mask + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_head.load(std::memory_order_relaxed);
            }
        }
    }

    /**
     * @brief Pops an item from the queue, blocking with cooperative yield/spin until available.
     *
     * @param out Reference receiving the popped element.
     * @param stoken Cooperative cancellation token.
     * @return true if popped, false if stop was requested.
     */
    bool pop(T& out, Common::Compat::stop_token stoken = { })
    {
        std::size_t pos = m_head.load(std::memory_order_relaxed);
        while (!stoken.stop_requested() && !m_stopped.load(std::memory_order_relaxed)) {
            Cell& cell = m_cells[pos & m_mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(cell.data);
                    cell.sequence.store(pos + m_mask + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                std::this_thread::yield();
                pos = m_head.load(std::memory_order_relaxed);
            } else {
                pos = m_head.load(std::memory_order_relaxed);
            }
        }
        return false;
    }

    /**
     * @brief Signals waiting threads and cancels blocking operations.
     */
    void stop() noexcept
    {
        m_stopped.store(true, std::memory_order_release);
    }

    /**
     * @brief Returns the total bounded capacity of the ring buffer.
     */
    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return m_capacity;
    }

    /**
     * @brief Returns approximate number of elements available in the queue.
     */
    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t currentHead = m_head.load(std::memory_order_relaxed);
        std::size_t currentTail = m_tail.load(std::memory_order_relaxed);
        return currentTail >= currentHead ? currentTail - currentHead : 0;
    }

    /**
     * @brief Checks if queue is currently empty.
     */
    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

private:
    struct alignas(64) Cell {
        std::atomic<std::size_t> sequence;
        T data { };
    };

    std::size_t m_capacity;
    std::size_t m_mask;
    std::unique_ptr<Cell[]> m_cells;

    alignas(64) std::atomic<std::size_t> m_tail { 0 };
    alignas(64) std::atomic<std::size_t> m_head { 0 };
    alignas(64) std::atomic<bool> m_stopped { false };
};

} // namespace Common::Queue

