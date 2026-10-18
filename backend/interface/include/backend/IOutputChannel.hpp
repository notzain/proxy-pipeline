#pragma once

#include <cstddef>
#include <span>

namespace Backend {

/**
 * @brief Abstract interface representing a message transmission channel.
 *
 * An output channel provides message transmission to an underlying transport endpoint
 * (e.g. writing files to a specific directory or sending UDP datagrams to a specific destination).
 */
class IOutputChannel {
public:
    virtual ~IOutputChannel() = default;

    /**
     * @brief Transmits a message through this output channel.
     *
     * @param message View of the byte buffer to transmit.
     * @return true If the message was successfully dispatched/written, false otherwise.
     */
    virtual bool send(std::span<const std::byte> message) = 0;
};

} // namespace Backend
