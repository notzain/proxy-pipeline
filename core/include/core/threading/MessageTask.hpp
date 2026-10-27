#pragma once

#include "common/compat/Span.hpp"
#include "common/memory/BufferSlice.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Threading {

/**
 * @brief Representation of a message task passed to background thread pool workers.
 *
 * Utilizes small buffer optimization (SBO) with an inline buffer sized to a full ingress
 * batch slot (2048 bytes), so channel pollers can receive datagrams directly into ring slot
 * memory (zero-copy forwarding). Larger (jumbo) payloads spill to a zero-copy pooled BufferSlice
 * or heap fallback.
 */
class MessageTask {
public:
    static constexpr std::size_t SmallBufferSize = 2048;

    MessageTask() = default;

    /**
     * @brief Assigns channel name and payload, using inline buffer if size <= SmallBufferSize.
     */
    void assign(std::string_view name, Common::Compat::span<const std::byte> data);

    /**
     * @brief Assigns a zero-copy pooled slice directly without heap allocation.
     */
    void assignSlice(std::string_view name, Common::Memory::BufferSlice slice, std::size_t size);

    /**
     * @brief Prepares the task for an in-place write and returns its writable inline buffer.
     *
     * Used for zero-copy ingress: a channel writes received bytes directly into the returned
     * span, then commitInline() records the size.
     *
     * @param name Channel name to associate with the task.
     * @return Common::Compat::span<std::byte> Writable inline buffer of SmallBufferSize bytes.
     */
    [[nodiscard]] Common::Compat::span<std::byte> prepareInline(std::string_view name);

    /**
     * @brief Finalizes an in-place write started with prepareInline().
     *
     * @param size Number of bytes written into the inline buffer (clamped to SmallBufferSize).
     */
    void commitInline(std::size_t size) noexcept;

    /**
     * @brief Constructs task with channel name and payload.
     */
    MessageTask(std::string name, Common::Compat::span<const std::byte> data);

    MessageTask(MessageTask&& other) noexcept;
    MessageTask& operator=(MessageTask&& other) noexcept;

    MessageTask(const MessageTask&) = delete;
    MessageTask& operator=(const MessageTask&) = delete;
    ~MessageTask() = default;

    /**
     * @brief Returns the channel name associated with the message.
     */
    [[nodiscard]] std::string_view channelName() const noexcept;

    /**
     * @brief Returns a non-owning span over the task payload data.
     */
    [[nodiscard]] Common::Compat::span<const std::byte> payload() const noexcept;

private:
    std::string m_channelName;
    std::array<std::byte, SmallBufferSize> m_inlineBuffer;
    std::size_t m_inlineSize { 0 };
    Common::Memory::BufferSlice m_pooledSlice;
    std::vector<std::byte> m_heapBuffer;
};

} // namespace Core::Threading
