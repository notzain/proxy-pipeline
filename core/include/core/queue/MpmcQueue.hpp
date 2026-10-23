#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>

namespace Core::Queue {

/**
 * @brief High-performance lock-free Multi-Producer Multi-Consumer (MPMC) bounded ring buffer.
 *
 * Implements Dmitry Vyukov's array-based bounded MPMC queue algorithm.
 * Each cell maintains an atomic sequence counter to coordinate concurrent producers and consumers
 * without holding mutexes.
 *
 * Cache-line alignment (alignas(64)) is applied to the head and tail position counters and
 * the queue cells to eliminate false sharing between concurrent push and pop threads.
 *
 * @tparam T Element type stored in the queue. Must be default constructible and movable.
 * @tparam Capacity Power-of-two queue capacity (>= 2).
 */
template <typename T, std::size_t Capacity = 4096>
    requires((Capacity & (Capacity - 1)) == 0 && Capacity >= 2)
class MpmcQueue {
public:
    static constexpr std::size_t BufferCapacity = Capacity;
    static constexpr std::size_t Mask = Capacity - 1;

    MpmcQueue()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            m_cells[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MpmcQueue()
    {
        stop();
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;
    MpmcQueue(MpmcQueue&&) = delete;
    MpmcQueue& operator=(MpmcQueue&&) = delete;

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
            Cell& cell = m_cells[pos & Mask];
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
    bool push(U&& data, std::stop_token stoken = { })
    {
        std::size_t pos = m_tail.load(std::memory_order_relaxed);
        while (!stoken.stop_requested() && !m_stopped.load(std::memory_order_relaxed)) {
            Cell& cell = m_cells[pos & Mask];
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
            Cell& cell = m_cells[pos & Mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(cell.data);
                    cell.sequence.store(pos + Mask + 1, std::memory_order_release);
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
     * @brief Pops an item from the queue, blocking with cooperative yield/spin until an item is available.
     *
     * @param out Reference receiving the popped element.
     * @param stoken Cooperative cancellation token.
     * @return true if popped, false if stop was requested.
     */
    bool pop(T& out, std::stop_token stoken = { })
    {
        std::size_t pos = m_head.load(std::memory_order_relaxed);
        while (!stoken.stop_requested() && !m_stopped.load(std::memory_order_relaxed)) {
            Cell& cell = m_cells[pos & Mask];
            std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = std::move(cell.data);
                    cell.sequence.store(pos + Mask + 1, std::memory_order_release);
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
     * @brief Pops up to maxCount elements from the queue in a batch.
     *
     * @tparam ConsumerFn Callable invoked as `fn(T&& item)`.
     * @param maxCount Maximum number of elements to pop.
     * @param consumerFn Function to process each popped element.
     * @return std::size_t Number of elements successfully popped.
     */
    template <typename ConsumerFn>
    std::size_t popBatch(std::size_t maxCount, ConsumerFn&& consumerFn)
    {
        std::size_t count = 0;
        T item;
        while (count < maxCount && tryPop(item)) {
            consumerFn(std::move(item));
            ++count;
        }
        return count;
    }

    /**
     * @brief Signals waiting threads and cancels blocking operations.
     */
    void stop() noexcept
    {
        m_stopped.store(true, std::memory_order_release);
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
        T data;
    };

    alignas(64) std::atomic<std::size_t> m_tail { 0 };
    alignas(64) std::atomic<std::size_t> m_head { 0 };
    alignas(64) std::atomic<bool> m_stopped { false };
    alignas(64) std::array<Cell, Capacity> m_cells;
};

} // namespace Core::Queue
