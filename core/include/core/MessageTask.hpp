#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @brief Representation of a message task passed to background thread pool workers.
 *
 * Utilizes small buffer optimization (SBO) with an inline buffer up to 256 bytes
 * to completely eliminate dynamic heap allocation for standard datagram payloads.
 */
class MessageTask {
public:
    static constexpr std::size_t SmallBufferSize = 256;

    MessageTask() = default;

    /**
     * @brief Assigns channel name and payload, using inline buffer if size <= SmallBufferSize.
     */
    void assign(std::string_view name, std::span<const std::byte> data);

    /**
     * @brief Constructs task with channel name and payload.
     */
    MessageTask(std::string name, std::span<const std::byte> data);

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
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;

private:
    std::string m_channelName;
    std::array<std::byte, SmallBufferSize> m_inlineBuffer;
    std::size_t m_inlineSize { 0 };
    std::vector<std::byte> m_heapBuffer;
};

} // namespace Core

