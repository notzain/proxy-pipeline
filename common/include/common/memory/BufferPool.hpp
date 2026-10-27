#pragma once

#include "common/memory/BufferSlice.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Common::Memory {

/**
 * @brief High-performance lock-free fixed-block memory slab pool.
 *
 * Pre-allocates a contiguous pool of 64-byte aligned blocks (e.g. 64 KB per block).
 * Acquiring a slice is wait-free via an atomic free-stack or bitmask/index ring,
 * completely avoiding malloc/free overhead during high-frequency datagram bursts.
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
    std::size_t m_blockSize;
    std::size_t m_blockCount;
    std::vector<std::byte> m_storage;

    // Free stack implemented as an array of block indices with an atomic head
    std::vector<std::size_t> m_freeIndices;
    alignas(64) std::atomic<std::size_t> m_freeTop { 0 };
    alignas(64) std::atomic<std::size_t> m_lock { 0 };
};

} // namespace Common::Memory
