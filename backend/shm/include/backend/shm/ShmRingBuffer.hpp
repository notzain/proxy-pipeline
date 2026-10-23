#pragma once

#include "backend/shm/sync/ShmSignaler.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Proxy::Backend::Shm {

/**
 * @brief Fixed-size slot in shared memory ring buffer.
 *
 * Aligned to 64-byte hardware cache lines with a 2048-byte total footprint.
 */
struct alignas(64) ShmSlot {
    uint32_t length { 0 };
    uint8_t payload[2044];
};

static_assert(sizeof(ShmSlot) == 2048, "ShmSlot must be exactly 2048 bytes");

/**
 * @brief Control block residing at the base of the shared memory region.
 *
 * Producer write index and consumer read index reside on separate 64-byte
 * cache lines to prevent cross-core false sharing.
 */
struct alignas(64) ShmHeader {
    static constexpr uint32_t Magic = 0x50525859; // 'PRXY'
    static constexpr uint32_t Version = 2; // Incremented for signaling sync block

    uint32_t magic { Magic };
    uint32_t version { Version };
    uint32_t capacity { 1024 };
    uint32_t slotPayloadSize { 2044 };

    uint8_t reserved1[48] { 0 };

    alignas(64) std::atomic<uint64_t> writeIndex { 0 };
    uint8_t reserved2[56] { 0 };

    alignas(64) std::atomic<uint64_t> readIndex { 0 };
    uint8_t reserved3[56] { 0 };

    alignas(64) ShmSyncBlock notEmptySync;
    alignas(64) ShmSyncBlock notFullSync;
};

/**
 * @brief Lockless single-producer single-consumer (SPSC) shared memory circular ring buffer.
 *
 * Operates over memory-mapped files or POSIX shared memory with zero system calls
 * during steady-state packet transmission and reception.
 */
class ShmRingBuffer {
public:
    static constexpr std::size_t DefaultCapacity = 1024;
    static constexpr std::size_t MaxPayloadBytes = 2044;

    /**
     * @brief Constructs and maps the shared memory ring buffer.
     *
     * @param path File system or shared memory path.
     * @param capacity Number of ring buffer slots (must be a power of two).
     */
    explicit ShmRingBuffer(std::string path, std::size_t capacity = DefaultCapacity);
    ~ShmRingBuffer();

    ShmRingBuffer(const ShmRingBuffer&) = delete;
    ShmRingBuffer& operator=(const ShmRingBuffer&) = delete;
    ShmRingBuffer(ShmRingBuffer&& other) noexcept;
    ShmRingBuffer& operator=(ShmRingBuffer&& other) noexcept;

    /**
     * @brief Checks if the shared memory region was mapped and initialized successfully.
     */
    [[nodiscard]] bool isValid() const noexcept;

    /**
     * @brief Writes a message into the next available ring buffer slot.
     *
     * @param message Raw payload span to write.
     * @param wasEmpty Optional out-parameter set to true if the ring buffer was empty prior to this push.
     * @return true if written successfully; false if queue is full or message is oversized.
     */
    bool push(std::span<const std::byte> message, bool* wasEmpty = nullptr);

    /**
     * @brief Vectorized transmission writing a batch of messages in a single atomic sequence update.
     *
     * @param messages Slices of messages to write.
     * @param wasEmpty Optional out-parameter set to true if the ring buffer was empty prior to this push.
     * @return Number of messages successfully written.
     */
    std::size_t pushBatch(std::span<const std::span<const std::byte>> messages, bool* wasEmpty = nullptr);

    /**
     * @brief Reads the next available message from the ring buffer into the destination buffer.
     *
     * @param destination Target buffer receiving the message bytes.
     * @param wasFull Optional out-parameter set to true if the ring buffer was completely full prior to this pop.
     * @return Number of bytes copied, or 0 if queue is empty.
     */
    std::size_t popInto(std::span<std::byte> destination, bool* wasFull = nullptr);

    /**
     * @brief Vectorized reception draining up to destinations.size() messages from the ring buffer.
     *
     * @param destinations Slices of destination memory to receive messages.
     * @param outSizes Array receiving the byte length of each transferred message.
     * @param wasFull Optional out-parameter set to true if the ring buffer was completely full prior to this pop.
     * @return Number of messages successfully received.
     */
    std::size_t popBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        bool* wasFull = nullptr);

    /**
     * @brief Returns current number of unread messages in the ring buffer.
     */
    [[nodiscard]] std::size_t size() const noexcept;

    /**
     * @brief Checks if the ring buffer contains no unread messages.
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Checks if the ring buffer has no free slots available.
     */
    [[nodiscard]] bool full() const noexcept;

    /**
     * @brief Resets read and write indices to zero.
     */
    void reset();

    /**
     * @brief Returns the path of the shared memory file.
     */
    [[nodiscard]] const std::string& getPath() const noexcept;

    /**
     * @brief Accessors for cross-process signaling sync blocks.
     */
    [[nodiscard]] ShmSyncBlock* notEmptySync() noexcept { return m_header ? &m_header->notEmptySync : nullptr; }
    [[nodiscard]] ShmSyncBlock* notFullSync() noexcept { return m_header ? &m_header->notFullSync : nullptr; }

private:
    std::string m_path;
    int m_fd { -1 };
    void* m_mappedMemory { nullptr };
    std::size_t m_mappedSize { 0 };
    ShmHeader* m_header { nullptr };
    ShmSlot* m_slots { nullptr };

    void mapMemory(std::size_t capacity);
    void unmapMemory();
};

} // namespace Proxy::Backend::Shm
