#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace Backend {

/**
 * @brief Abstract interface representing a message transmission channel.
 *
 * An output channel provides message transmission to an underlying transport endpoint
 * (e.g. writing files to a specific directory or sending UDP datagrams to a specific destination).
 * Parallel execution can call send() or sendBatch() concurrently on a shared output channel.
 * Implementations must support concurrent calls or callers must synchronize access.
 */
class IOutputChannel {
public:
    explicit IOutputChannel(std::string name = "");
    IOutputChannel(const IOutputChannel&) = default;
    IOutputChannel& operator=(const IOutputChannel&) = default;
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
    virtual bool send(Common::Compat::span<const std::byte> message) = 0;

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
    virtual std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages);

protected:
    IOutputChannel(IOutputChannel&&) noexcept = default;
    IOutputChannel& operator=(IOutputChannel&&) noexcept = default;

private:
    std::string m_name;
};

} // namespace Backend
