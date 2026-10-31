#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>

namespace Common::Net {

/**
 * @brief Simple buffer and size pair used by low-level socket pollers.
 */
struct PollerBufferSlot {
    Common::Compat::span<std::byte> buffer { };
    std::size_t size { 0 };

    PollerBufferSlot() noexcept = default;
    constexpr PollerBufferSlot(Common::Compat::span<std::byte> buf, std::size_t sz = 0) noexcept
        : buffer(buf)
        , size(sz)
    {
    }
};

} // namespace Common::Net
