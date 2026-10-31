#pragma once

#include <chrono>
#include <cstdint>

namespace Common::Compat {

/**
 * @brief UTC Unix epoch time point (1970-01-01 00:00:00 UTC) with nanosecond precision.
 *
 * In C++20, std::chrono::system_clock is formally mandated to measure time since the Unix epoch in UTC.
 * On all supported POSIX (Linux, macOS) and MSVC targets, std::chrono::system_clock has always measured
 * UTC time since 1970-01-01 00:00:00 UTC, providing seamless C++17 and C++20 compatibility.
 */
using UtcTimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

/**
 * @brief Returns current UTC time since Unix epoch as a UtcTimePoint.
 */
[[nodiscard]] UtcTimePoint nowUtc() noexcept;

/**
 * @brief Converts a UtcTimePoint to nanoseconds since UNIX epoch (1970-01-01 00:00:00 UTC).
 */
[[nodiscard]] std::uint64_t toEpochNs(UtcTimePoint tp) noexcept;

/**
 * @brief Constructs a UtcTimePoint from nanoseconds since UNIX epoch.
 */
[[nodiscard]] UtcTimePoint fromEpochNs(std::uint64_t ns) noexcept;

} // namespace Common::Compat
