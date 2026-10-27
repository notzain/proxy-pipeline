#pragma once

#include "common/compat/Span.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace Common::Memory {

/**
 * @brief High-performance stack-allocated container with dynamic heap spillover for small span buffers.
 *
 * Designed to eliminate heap allocations for typical message batch sizes (e.g., <= 64 messages)
 * during vectorized ingress and egress processing, while gracefully spilling to heap if a jumbo
 * batch exceeds the inline stack capacity.
 *
 * @tparam T Element type (typically `Common::Compat::span<const std::byte>`).
 * @tparam InlineCapacity Maximum number of elements to store inline on the stack (defaults to 64).
 */
template <typename T = Common::Compat::span<const std::byte>, std::size_t InlineCapacity = 64>
class SmallSpanBuffer {
public:
    /**
     * @brief Constructs a SmallSpanBuffer reserving capacity for at least `count` elements.
     *
     * @param count Initial required capacity.
     */
    explicit SmallSpanBuffer(std::size_t count)
        : m_capacity(count)
    {
        if (count > InlineCapacity) {
            m_heap.resize(count);
            m_data = m_heap.data();
        } else {
            m_data = m_stack.data();
        }
    }

    ~SmallSpanBuffer() = default;

    SmallSpanBuffer(const SmallSpanBuffer&) = delete;
    SmallSpanBuffer& operator=(const SmallSpanBuffer&) = delete;
    SmallSpanBuffer(SmallSpanBuffer&&) noexcept = default;
    SmallSpanBuffer& operator=(SmallSpanBuffer&&) noexcept = default;

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }

    [[nodiscard]] T& operator[](std::size_t idx) noexcept { return m_data[idx]; }
    [[nodiscard]] const T& operator[](std::size_t idx) const noexcept { return m_data[idx]; }

    [[nodiscard]] std::size_t size() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    [[nodiscard]] Common::Compat::span<T> span() noexcept
    {
        return Common::Compat::span<T>(m_data, m_capacity);
    }

    [[nodiscard]] Common::Compat::span<T> span(std::size_t count) noexcept
    {
        return Common::Compat::span<T>(m_data, std::min(count, m_capacity));
    }

    [[nodiscard]] Common::Compat::span<const T> span() const noexcept
    {
        return Common::Compat::span<const T>(m_data, m_capacity);
    }

    [[nodiscard]] Common::Compat::span<const T> span(std::size_t count) const noexcept
    {
        return Common::Compat::span<const T>(m_data, std::min(count, m_capacity));
    }

    [[nodiscard]] Common::Compat::span<const T> constSpan() const noexcept
    {
        return Common::Compat::span<const T>(m_data, m_capacity);
    }

    [[nodiscard]] auto begin() noexcept { return m_data; }
    [[nodiscard]] auto end() noexcept { return m_data + m_capacity; }
    [[nodiscard]] auto begin() const noexcept { return m_data; }
    [[nodiscard]] auto end() const noexcept { return m_data + m_capacity; }

private:
    std::size_t m_capacity { 0 };
    std::array<T, InlineCapacity> m_stack;
    std::vector<T> m_heap;
    T* m_data { nullptr };
};

} // namespace Common::Memory
