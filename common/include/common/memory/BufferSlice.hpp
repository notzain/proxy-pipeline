#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <memory>

namespace Common::Memory {

class BufferPool;

/**
 * @brief Reference-counted or pool-owned slice of memory backed by a BufferPool block.
 *
 * When a BufferSlice goes out of scope or is destroyed, it automatically releases its block
 * back to the owning BufferPool.
 */
class BufferSlice {
public:
    BufferSlice() noexcept;

    /**
     * @brief Constructs a slice owning a block from a pool.
     */
    BufferSlice(std::byte* data, std::size_t size, std::size_t blockIndex, std::shared_ptr<BufferPool> pool) noexcept;

    ~BufferSlice();

    BufferSlice(BufferSlice&& other) noexcept;
    BufferSlice& operator=(BufferSlice&& other) noexcept;

    BufferSlice(const BufferSlice&) = delete;
    BufferSlice& operator=(const BufferSlice&) = delete;

    [[nodiscard]] Common::Compat::span<std::byte> span() noexcept;
    [[nodiscard]] Common::Compat::span<const std::byte> span() const noexcept;
    [[nodiscard]] std::byte* data() noexcept;
    [[nodiscard]] const std::byte* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    void resize(std::size_t newSize) noexcept;
    void reset() noexcept;

private:
    std::byte* m_data { nullptr };
    std::size_t m_size { 0 };
    std::size_t m_blockIndex { 0 };
    std::shared_ptr<BufferPool> m_pool;
};

} // namespace Common::Memory
