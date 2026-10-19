#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>
#include <type_traits>

namespace Core {

/**
 * @brief High-performance lockless Single-Producer Single-Consumer (SPSC) ring buffer.
 *
 * Utilizes cache-line aligned (alignas(64)) atomic head and tail pointers with power-of-two
 * bitmask wrapping to eliminate false sharing.
 *
 * Employs C++20 atomic wait/notify semantics backed by monotonic version counters to guarantee
 * instant wakeup during shutdown without spurious wait deadlocks across platforms.
 *
 * Producer synchronization mutex is provided for multi-producer serialized scenarios while
 * keeping the consumer path strictly lockless and wait-free under normal streaming conditions.
 *
 * @tparam T Element type stored in the ring buffer.
 * @tparam Capacity Power-of-two queue capacity.
 */
template <typename T, std::size_t Capacity = 4096>
    requires((Capacity & (Capacity - 1)) == 0 && Capacity >= 2)
class SpscQueue {
public:
    static constexpr std::size_t BufferCapacity = Capacity;
    static constexpr std::size_t Mask = Capacity - 1;

    SpscQueue() = default;
    ~SpscQueue()
    {
        stop();
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    /**
     * @brief Pushes a batch of elements into the ring buffer.
     *
     * Serializes concurrent producer calls with an internal lock while keeping the worker consumer lock-free.
     *
     * @tparam ProducerFn Callable invoked as `fn(slotIndex, elementIndex)` to populate ring slots.
     * @param count Number of elements to push.
     * @param producerFn Function to populate each ring slot.
     * @param stoken Cooperative cancellation token.
     * @return true if elements were written, false if stopped before writing.
     */
    template <typename ProducerFn>
    bool pushBatch(std::size_t count, ProducerFn&& producerFn, std::stop_token stoken = { })
    {
        if (count == 0 || count > Capacity) {
            return false;
        }

        std::lock_guard lock(m_producerMutex);

        std::size_t currentTail = m_tail.load(std::memory_order_relaxed);
        while (!stoken.stop_requested()) {
            std::size_t currentHead = m_head.load(std::memory_order_acquire);
            if (currentTail - currentHead + count <= Capacity) {
                break;
            }
            std::uint32_t currentVer = m_headVersion.load(std::memory_order_acquire);
            currentHead = m_head.load(std::memory_order_acquire);
            if (currentTail - currentHead + count > Capacity) {
                m_headVersion.wait(currentVer, std::memory_order_relaxed);
            }
        }

        if (stoken.stop_requested()) {
            return false;
        }

        for (std::size_t i = 0; i < count; ++i) {
            producerFn(m_ring[(currentTail + i) & Mask], i);
        }

        m_tail.store(currentTail + count, std::memory_order_release);
        m_tailVersion.fetch_add(1, std::memory_order_release);
        m_tailVersion.notify_one();
        return true;
    }

    /**
     * @brief Drains up to maxCount elements from the ring buffer into a consumer callback.
     *
     * The callback is passed a contiguous std::span of elements directly inside the ring buffer.
     * Completely lock-free.
     *
     * @tparam ConsumerFn Callable invoked as `fn(std::span<const T> slice)`.
     * @param maxCount Maximum number of elements to process.
     * @param consumerFn Function to process contiguous elements.
     * @param stoken Cooperative cancellation token.
     * @return Number of elements drained, or 0 if empty / stopped.
     */
    template <typename ConsumerFn>
    std::size_t drainContiguous(std::size_t maxCount, ConsumerFn&& consumerFn, std::stop_token stoken = { })
    {
        std::size_t currentHead = m_head.load(std::memory_order_relaxed);
        std::size_t currentTail = m_tail.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            if (stoken.stop_requested()) {
                return 0;
            }
            std::uint32_t currentVer = m_tailVersion.load(std::memory_order_acquire);
            currentTail = m_tail.load(std::memory_order_acquire);
            if (currentHead == currentTail) {
                m_tailVersion.wait(currentVer, std::memory_order_relaxed);
                currentTail = m_tail.load(std::memory_order_acquire);
                if (currentHead == currentTail) {
                    return 0;
                }
            }
        }

        std::size_t available = currentTail - currentHead;
        std::size_t drainCount = std::min(available, maxCount);

        std::size_t ringIndex = currentHead & Mask;
        drainCount = std::min(drainCount, Capacity - ringIndex);

        consumerFn(std::span<const T>(&m_ring[ringIndex], drainCount));

        m_head.store(currentHead + drainCount, std::memory_order_release);
        m_headVersion.fetch_add(1, std::memory_order_release);
        m_headVersion.notify_one();
        return drainCount;
    }

    /**
     * @brief Signals waiting threads and cancels blocking atomic waits.
     */
    void stop() noexcept
    {
        m_tailVersion.fetch_add(1, std::memory_order_release);
        m_tailVersion.notify_all();
        m_headVersion.fetch_add(1, std::memory_order_release);
        m_headVersion.notify_all();
    }

    /**
     * @brief Returns approximate number of elements available in the queue.
     */
    [[nodiscard]] std::size_t size() const noexcept
    {
        std::size_t currentHead = m_head.load(std::memory_order_relaxed);
        std::size_t currentTail = m_tail.load(std::memory_order_acquire);
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
    alignas(64) std::atomic<std::size_t> m_tail { 0 };
    alignas(64) std::atomic<std::uint32_t> m_tailVersion { 0 };
    alignas(64) std::atomic<std::size_t> m_head { 0 };
    alignas(64) std::atomic<std::uint32_t> m_headVersion { 0 };
    alignas(64) std::mutex m_producerMutex;
    alignas(64) std::array<T, Capacity> m_ring;
};

} // namespace Core
