#include "common/compat/Time.hpp"

namespace Common::Compat {

UtcTimePoint nowUtc() noexcept
{
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now());
}

std::uint64_t toEpochNs(UtcTimePoint tp) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
}

UtcTimePoint fromEpochNs(std::uint64_t ns) noexcept
{
    return UtcTimePoint(std::chrono::nanoseconds(ns));
}

} // namespace Common::Compat

