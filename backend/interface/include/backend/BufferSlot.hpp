#pragma once

#include "backend/Lease.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>

namespace Backend {

/**
 * @brief Cohesive target buffer, received byte counter and ownership lease for vectorized batch ingress.
 *
 * Encapsulates the target memory buffer and tracks the number of bytes transferred into it.
 * Replaces parallel destination and size spans in `IInputChannel::receiveBatch` and `ISocketPoller::receiveBatch`.
 *
 * Transports whose messages are owned by an external API attach a `Lease` that the consumer must
 * settle (or move onward) once the message has been processed. Transports owning their buffers leave
 * it empty.
 */
struct BufferSlot {
    Common::Compat::span<std::byte> buffer { };
    std::size_t size { 0 };
    Lease lease { };

    /**
     * @brief Returns a non-owning span over the populated bytes.
     */
    [[nodiscard]] Common::Compat::span<const std::byte> filledSpan() const noexcept
    {
        return buffer.subspan(0, size);
    }

    /**
     * @brief Returns a mutable span over the populated bytes.
     */
    [[nodiscard]] Common::Compat::span<std::byte> filledSpan() noexcept
    {
        return buffer.subspan(0, size);
    }
};

} // namespace Backend
