#pragma once

#include <cstdint>

namespace Common::Diagnostics {

[[nodiscard]] constexpr bool shouldLogEventCount(std::uint64_t count) noexcept
{
    return count <= 8 || count % 1024 == 0;
}

} // namespace Common::Diagnostics
