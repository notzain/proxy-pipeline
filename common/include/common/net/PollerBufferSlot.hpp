#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>

namespace Common::Net {

/**
 * @brief Buffer and receive status used by low-level socket pollers.
 *
 * A truncated datagram has `size == 0` and `truncated == true`; its partial bytes must not be consumed.
 */
struct PollerBufferSlot {
    Common::Compat::span<std::byte> buffer { };
    std::size_t size { 0 };
    bool truncated { false };

    PollerBufferSlot() noexcept = default;
    constexpr PollerBufferSlot(Common::Compat::span<std::byte> buf, std::size_t sz = 0, bool wasTruncated = false) noexcept
        : buffer(buf)
        , size(sz)
        , truncated(wasTruncated)
    {
    }
};

} // namespace Common::Net
