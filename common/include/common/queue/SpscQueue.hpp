#pragma once

#include "common/compat/AtomicWait.hpp"
#include "common/compat/Span.hpp"
#include "common/compat/StopToken.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace Common::Queue {

/**
 * @brief High-performance lockless Single-Producer Single-Consumer (SPSC) ring buffer.
 *
 * Utilizes cache-line aligned (alignas(64)) atomic head and tail pointers with power-of-two
 * bitmask wrapping to eliminate false sharing.
 *
 * Employs atomic wait/notify semantics backed by monotonic version counters to guarantee
 * instant wakeup during shutdown without spurious wait deadlocks across platforms.
 *
 * Producer synchronization mutex is provided for multi-producer serialized scenarios while
 * keeping the consumer path strictly lockless and wait-free under normal streaming conditions.
 *
 * @tparam T Element type stored in the ring buffer.
 * @tparam Capacity Power-of-two queue capacity.
 */
template <typename T, std::size_t Capacity = 4096, typename = void>
class SpscQueue;

template <typename T, std::size_t Capacity>
class SpscQueue<T, Capacity, std::enable_if_t<((Capacity & (Capacity - 1)) == 0 && Capacity >= 2)>> {
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
    bool pushBatch(std::size_t count, ProducerFn&& producerFn, Common::Compat::stop_token stoken = { })
    {
        if (count == 0 || count > Capacity) {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_producerMutex);

        std::size_t currentTail = m_tail.load(std::memory_order_relaxed);
        while (!stoken.stop_requested()) {
            std::size_t currentHead = m_head.load(std::memory_order_acquire);
            if (currentTail - currentHead + count <= Capacity) {
                break;
            }
            std::uint32_t currentVer = m_headVersion.load(std::memory_order_acquire);
            currentHead = m_head.load(std::memory_order_acquire);
            if (currentTail - currentHead + count > Capacity) {
                Common::Compat::atomic_wait(m_headVersion, currentVer, std::memory_order_relaxed);
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
        Common::Compat::atomic_notify_one(m_tailVersion);
        return true;
    }

    /**
     * @brief Exposes a contiguous run of free ring slots for in-place (zero-copy) population.
     *
     * Blocks until at least one slot is free. The filler receives a writable span of up to
     * maxCount contiguous slots (bounded by free space and the physical end of the ring) and
     * returns how many of them it actually populated; only that many are published.
     *
     * @tparam FillFn Callable invoked as `std::size_t fn(Common::Compat::span<T> slots)`.
     * @param maxCount Maximum number of slots to expose.
     * @param fillFn Function writing directly into ring slots.
     * @param stoken Cooperative cancellation token.
     * @return Number of elements published (0 if stopped or nothing filled).
     */
    template <typename FillFn>
    std::size_t pushInPlace(std::size_t maxCount, FillFn&& fillFn, Common::Compat::stop_token stoken = { })
    {
        std::unique_lock<std::mutex> lock(m_producerMutex);
        return pushInPlaceLocked(maxCount, std::forward<FillFn>(fillFn), stoken);
    }

    /**
     * @brief Non-blocking variant of pushInPlace() regarding the producer lock.
     *
     * Returns std::nullopt immediately if another producer currently holds the queue,
     * allowing callers to try a different queue instead of stalling.
     */
    template <typename FillFn>
    std::optional<std::size_t> tryPushInPlace(std::size_t maxCount, FillFn&& fillFn, Common::Compat::stop_token stoken = { })
    {
        std::unique_lock<std::mutex> lock(m_producerMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return std::nullopt;
        }
        return pushInPlaceLocked(maxCount, std::forward<FillFn>(fillFn), stoken);
    }

    /**
     * @brief Drains up to maxCount elements from the ring buffer into a consumer callback.
     *
     * The callback is passed a contiguous Common::Compat::span of elements directly inside the ring buffer.
     * Completely lock-free.
     *
     * @tparam ConsumerFn Callable invoked as `fn(Common::Compat::span<const T> slice)`.
     * @param maxCount Maximum number of elements to process.
     * @param consumerFn Function to process contiguous elements.
     * @param stoken Cooperative cancellation token.
     * @return Number of elements drained, or 0 if empty / stopped.
     */
    template <typename ConsumerFn>
    std::size_t drainContiguous(std::size_t maxCount, ConsumerFn&& consumerFn, Common::Compat::stop_token stoken = { })
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
                Common::Compat::atomic_wait(m_tailVersion, currentVer, std::memory_order_relaxed);
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

        consumerFn(Common::Compat::span<const T>(&m_ring[ringIndex], drainCount));

        m_head.store(currentHead + drainCount, std::memory_order_release);
        m_headVersion.fetch_add(1, std::memory_order_release);
        Common::Compat::atomic_notify_one(m_headVersion);
        return drainCount;
    }

    /**
     * @brief Drains up to maxCount elements from the ring buffer into a consumer callback without blocking.
     *
     * Returns 0 immediately if the queue is empty. Completely lock-free and non-blocking.
     */
    template <typename ConsumerFn>
    std::size_t tryDrainContiguous(std::size_t maxCount, ConsumerFn&& consumerFn)
    {
        std::size_t currentHead = m_head.load(std::memory_order_relaxed);
        std::size_t currentTail = m_tail.load(std::memory_order_acquire);

        if (currentHead == currentTail) {
            return 0;
        }

        std::size_t available = currentTail - currentHead;
        std::size_t drainCount = std::min(available, maxCount);

        std::size_t ringIndex = currentHead & Mask;
        drainCount = std::min(drainCount, Capacity - ringIndex);

        consumerFn(Common::Compat::span<const T>(&m_ring[ringIndex], drainCount));

        m_head.store(currentHead + drainCount, std::memory_order_release);
        m_headVersion.fetch_add(1, std::memory_order_release);
        Common::Compat::atomic_notify_one(m_headVersion);
        return drainCount;
    }

    /**
     * @brief Signals waiting threads and cancels blocking atomic waits.
     */
    void stop() noexcept
    {
        m_tailVersion.fetch_add(1, std::memory_order_release);
        Common::Compat::atomic_notify_all(m_tailVersion);
        m_headVersion.fetch_add(1, std::memory_order_release);
        Common::Compat::atomic_notify_all(m_headVersion);
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
    template <typename FillFn>
    std::size_t pushInPlaceLocked(std::size_t maxCount, FillFn&& fillFn, Common::Compat::stop_token stoken)
    {
        if (maxCount == 0) {
            return 0;
        }

        const std::size_t currentTail = m_tail.load(std::memory_order_relaxed);
        std::size_t freeSlots = 0;
        while (!stoken.stop_requested()) {
            freeSlots = Capacity - (currentTail - m_head.load(std::memory_order_acquire));
            if (freeSlots > 0) {
                break;
            }
            std::uint32_t currentVer = m_headVersion.load(std::memory_order_acquire);
            if (Capacity - (currentTail - m_head.load(std::memory_order_acquire)) == 0) {
                Common::Compat::atomic_wait(m_headVersion, currentVer, std::memory_order_relaxed);
            }
        }
        if (stoken.stop_requested() || freeSlots == 0) {
            return 0;
        }

        const std::size_t ringIndex = currentTail & Mask;
        const std::size_t span = std::min({ maxCount, freeSlots, Capacity - ringIndex });

        std::size_t filled = fillFn(Common::Compat::span<T>(&m_ring[ringIndex], span));
        filled = std::min(filled, span);
        if (filled == 0) {
            return 0;
        }

        m_tail.store(currentTail + filled, std::memory_order_release);
        m_tailVersion.fetch_add(1, std::memory_order_release);
        Common::Compat::atomic_notify_one(m_tailVersion);
        return filled;
    }

    alignas(64) std::atomic<std::size_t> m_tail { 0 };
    alignas(64) std::atomic<std::uint32_t> m_tailVersion { 0 };
    alignas(64) std::atomic<std::size_t> m_head { 0 };
    alignas(64) std::atomic<std::uint32_t> m_headVersion { 0 };
    alignas(64) std::mutex m_producerMutex;
    alignas(64) std::array<T, Capacity> m_ring;
};

} // namespace Common::Queue
