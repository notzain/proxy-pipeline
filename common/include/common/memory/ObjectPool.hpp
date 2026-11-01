#pragma once

#include "common/queue/BoundedMpmcQueue.hpp"
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace Common::Memory {

template <typename T>
class ObjectPool;

/**
 * @brief Move-only RAII handle representing a borrowed object lease from an ObjectPool.
 *
 * Symmetrical to BufferSlice in Common::Memory::BufferPool.
 * When an ObjectHandle is destroyed or goes out of scope, it automatically returns
 * the borrowed object back to its owning ObjectPool.
 *
 * To transfer raw ownership to an external lifecycle manager (such as a SettlementToken),
 * call release() to detach the object from the handle without recycling.
 *
 * @tparam T Object type.
 */
template <typename T>
class ObjectHandle {
public:
    constexpr ObjectHandle() noexcept = default;

    ObjectHandle(T* ptr, ObjectPool<T>* pool) noexcept
        : m_ptr(ptr)
        , m_pool(pool)
    {
    }

    ~ObjectHandle()
    {
        reset();
    }

    ObjectHandle(ObjectHandle&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr))
        , m_pool(std::exchange(other.m_pool, nullptr))
    {
    }

    ObjectHandle& operator=(ObjectHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
            m_pool = std::exchange(other.m_pool, nullptr);
        }
        return *this;
    }

    ObjectHandle(const ObjectHandle&) = delete;
    ObjectHandle& operator=(const ObjectHandle&) = delete;

    [[nodiscard]] T* get() noexcept
    {
        return m_ptr;
    }

    [[nodiscard]] const T* get() const noexcept
    {
        return m_ptr;
    }

    T* operator->() noexcept
    {
        return m_ptr;
    }

    const T* operator->() const noexcept
    {
        return m_ptr;
    }

    T& operator*() noexcept
    {
        return *m_ptr;
    }

    const T& operator*() const noexcept
    {
        return *m_ptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_ptr != nullptr;
    }

    /**
     * @brief Detaches the object from the handle and returns the raw pointer without recycling.
     */
    [[nodiscard]] T* release() noexcept
    {
        m_pool = nullptr;
        return std::exchange(m_ptr, nullptr);
    }

    /**
     * @brief Manually recycles the object back to the pool immediately.
     */
    void reset() noexcept;

private:
    T* m_ptr { nullptr };
    ObjectPool<T>* m_pool { nullptr };
};

/**
 * @brief Thread-safe, lock-free bounded object pool with elastic burst overflow fallback.
 *
 * Pre-allocates N unique instances of T into a BoundedMpmcQueue.
 * During steady state, acquiring and releasing objects incurs zero heap allocations.
 * If all pre-allocated objects are in flight and allowOverflow is true, the pool
 * dynamically creates elastic instances to absorb bursts. When traffic subsides and
 * burst objects are returned, they are rejected by the full queue and automatically
 * deallocated back to the OS via std::unique_ptr.
 *
 * @tparam T Object type managed by the pool. Must be default constructible.
 */
template <typename T>
class ObjectPool {
public:
    static constexpr std::size_t DefaultCapacity = 1024;

    explicit ObjectPool(std::size_t capacity = DefaultCapacity, bool allowOverflow = true)
        : m_capacity(capacity > 0 ? capacity : DefaultCapacity)
        , m_allowOverflow(allowOverflow)
        , m_availableObjects(m_capacity)
    {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            m_availableObjects.tryPush(std::make_unique<T>());
        }
    }

    ~ObjectPool() = default;

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    /**
     * @brief Acquires an idle object from the pool without blocking, wrapped in an ObjectHandle.
     *
     * When the returned ObjectHandle goes out of scope, it automatically returns the object
     * back to this pool.
     *
     * @return ObjectHandle<T> owning the lease on the object, or empty handle if exhausted.
     */
    [[nodiscard]] ObjectHandle<T> acquire() noexcept
    {
        std::unique_ptr<T> item;
        if (m_availableObjects.tryPop(item)) {
            return ObjectHandle<T>(item.release(), this);
        }

        if (m_allowOverflow) {
            try {
                auto overflowItem = std::make_unique<T>();
                return ObjectHandle<T>(overflowItem.release(), this);
            } catch (const std::bad_alloc&) {
                return ObjectHandle<T>();
            }
        }

        return ObjectHandle<T>();
    }

    /**
     * @brief Returns a raw pointer back to the pool as an internal recycling mechanism.
     */
    void recycleRaw(T* ptr) noexcept
    {
        if (!ptr) {
            return;
        }
        std::unique_ptr<T> item(ptr);
        if (!m_allowOverflow) {
            // Under fixed capacity, ensure the node is pushed back
            m_availableObjects.push(std::move(item));
        } else {
            m_availableObjects.tryPush(std::move(item));
        }
    }

    /**
     * @brief Returns an object back to the pool.
     */
    void release(std::unique_ptr<T> item) noexcept
    {
        if (!item) {
            return;
        }
        if (!m_allowOverflow) {
            m_availableObjects.push(std::move(item));
        } else {
            m_availableObjects.tryPush(std::move(item));
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return m_capacity;
    }

    [[nodiscard]] std::size_t available() const noexcept
    {
        return m_availableObjects.size();
    }

private:
    std::size_t m_capacity;
    bool m_allowOverflow;
    Common::Queue::BoundedMpmcQueue<std::unique_ptr<T>> m_availableObjects;
};

template <typename T>
void ObjectHandle<T>::reset() noexcept
{
    if (m_ptr && m_pool) {
        m_pool->recycleRaw(m_ptr);
    } else if (m_ptr) {
        delete m_ptr;
    }
    m_ptr = nullptr;
    m_pool = nullptr;
}

} // namespace Common::Memory
