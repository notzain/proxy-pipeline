#pragma once

#include "common/memory/BufferSlice.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Common::Memory {

/**
 * @brief High-performance lock-free fixed-block memory slab pool.
 *
 * Pre-allocates a contiguous pool of blocks (e.g. 64 KB per block).
 * Acquiring and releasing slices are lock-free via a 64-bit tagged Treiber free-list
 * (32-bit ABA tag + 32-bit next-index), completely eliminating spinlock contention
 * across concurrent poller and worker threads.
 */
class BufferPool : public std::enable_shared_from_this<BufferPool> {
public:
    static constexpr std::size_t DefaultBlockSize = 65536;
    static constexpr std::size_t DefaultBlockCount = 256;

    explicit BufferPool(std::size_t blockSize = DefaultBlockSize, std::size_t blockCount = DefaultBlockCount);
    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) = delete;
    BufferPool& operator=(BufferPool&&) = delete;

    /**
     * @brief Acquires a slice from the pool. Returns std::nullopt if the pool is exhausted.
     */
    [[nodiscard]] std::optional<BufferSlice> acquire();

    /**
     * @brief Releases a block back to the pool. Called automatically by BufferSlice destructor.
     */
    void release(std::size_t blockIndex) noexcept;

    [[nodiscard]] std::size_t blockSize() const noexcept;
    [[nodiscard]] std::size_t blockCount() const noexcept;
    [[nodiscard]] std::size_t available() const noexcept;

private:
    static constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFU;

    [[nodiscard]] static constexpr std::uint64_t packHead(std::uint32_t tag, std::uint32_t index) noexcept
    {
        return (static_cast<std::uint64_t>(tag) << 32U) | static_cast<std::uint64_t>(index);
    }

    [[nodiscard]] static constexpr std::uint32_t unpackTag(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head >> 32U);
    }

    [[nodiscard]] static constexpr std::uint32_t unpackIndex(std::uint64_t head) noexcept
    {
        return static_cast<std::uint32_t>(head & 0xFFFFFFFFULL);
    }

    std::size_t m_blockSize;
    std::size_t m_blockCount;
    std::vector<std::byte> m_storage;

    // Lock-free tagged Treiber stack of free block indices
    std::vector<std::uint32_t> m_nextFree;
    alignas(64) std::atomic<std::uint64_t> m_head { packHead(0, InvalidIndex) };
    alignas(64) std::atomic<std::size_t> m_available { 0 };
};

} // namespace Common::Memory
