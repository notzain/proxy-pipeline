#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>

namespace Backend {

/**
 * @brief Cohesive target buffer and received byte counter for vectorized batch ingress.
 *
 * Encapsulates the target memory buffer and tracks the number of bytes transferred into it.
 * Replaces parallel destination and size spans in `IInputChannel::receiveBatch` and `ISocketPoller::receiveBatch`.
 */
struct BufferSlot {
    Common::Compat::span<std::byte> buffer { };
    std::size_t size { 0 };

    /**
     * @brief Returns a non-owning span over the populated bytes.
     */
    [[nodiscard]] constexpr Common::Compat::span<const std::byte> filledSpan() const noexcept
    {
        return buffer.subspan(0, size);
    }

    /**
     * @brief Returns a mutable span over the populated bytes.
     */
    [[nodiscard]] constexpr Common::Compat::span<std::byte> filledSpan() noexcept
    {
        return buffer.subspan(0, size);
    }
};

} // namespace Backend
