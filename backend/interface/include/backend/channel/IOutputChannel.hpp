#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace Backend {

class MessageView;

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
     * @brief Transmits a raw or pre-packed byte buffer through this output channel.
     *
     * @param message View of the byte buffer to transmit.
     * @return true If the message was successfully dispatched/written, false otherwise.
     */
    virtual bool send(Common::Compat::span<const std::byte> message) = 0;

    /**
     * @brief Transmits an already-formed in-flight MessageView through this output channel.
     *
     * Default implementation forwards `message.rawBuffer()`. Transports that own in-flight
     * message objects (e.g. VendorOutputChannel) override this to consume and forward the
     * underlying message instance directly, falling back to constructing a new message from
     * `message.payload()` if the instance was already consumed by an earlier send.
     *
     * @param message In-flight message view to forward.
     * @return true If the message was successfully dispatched, false if packing failed or the transport rejected it.
     */
    virtual bool send(const MessageView& message);

    /**
     * @brief Transmits a batch of raw byte buffers through this output channel.
     *
     * Transports may implement vectorized batch sending (e.g. ::sendmmsg on Linux)
     * to dispatch multiple datagrams in a single system call.
     * Default implementation iterates over send().
     *
     * @param messages View of message byte spans to transmit.
     * @return std::size_t Number of messages successfully dispatched.
     */
    virtual std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages);

    /**
     * @brief Transmits a batch of in-flight MessageView instances through this output channel.
     *
     * Default implementation extracts `rawBuffer()` spans and delegates to the vectorized
     * byte-span `sendBatch()`. Transports that consume in-flight message objects override
     * this or delegate per-message to `send(const MessageView&)`.
     *
     * @param messages View of in-flight MessageView instances to transmit.
     * @return std::size_t Number of messages successfully dispatched.
     */
    virtual std::size_t sendBatch(Common::Compat::span<const MessageView> messages);

protected:
    IOutputChannel(IOutputChannel&&) noexcept = default;
    IOutputChannel& operator=(IOutputChannel&&) noexcept = default;

private:
    std::string m_name;
};

} // namespace Backend
