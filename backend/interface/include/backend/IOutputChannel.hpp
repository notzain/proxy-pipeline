#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Backend {

/**
 * @brief Abstract interface representing a message transmission channel.
 *
 * An output channel provides message transmission to an underlying transport endpoint
 * (e.g. writing files to a specific directory or sending UDP datagrams to a specific destination).
 */
class IOutputChannel {
public:
    explicit IOutputChannel(std::string name = "");
    virtual ~IOutputChannel() = default;

    /**
     * @brief Gets the logical identifier or configuration name of this channel.
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Transmits a message through this output channel.
     *
     * @param message View of the byte buffer to transmit.
     * @return true If the message was successfully dispatched/written, false otherwise.
     */
    virtual bool send(std::span<const std::byte> message) = 0;

    /**
     * @brief Transmits a batch of messages through this output channel.
     *
     * Transports may implement vectorized batch sending (e.g. ::sendmmsg on Linux)
     * to dispatch multiple datagrams in a single system call.
     * Default implementation iterates over send().
     *
     * @param messages View of message byte spans to transmit.
     * @return std::size_t Number of messages successfully dispatched.
     */
    virtual std::size_t sendBatch(std::span<const std::span<const std::byte>> messages);

private:
    std::string m_name;
};

} // namespace Backend
