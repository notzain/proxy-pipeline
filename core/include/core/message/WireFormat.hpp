#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace Core::Message::Wire {

inline void writeUint16Be(std::byte* dst, std::uint16_t value) noexcept
{
    dst[0] = static_cast<std::byte>((value >> 8) & 0xFF);
    dst[1] = static_cast<std::byte>(value & 0xFF);
}

inline void writeUint32Be(std::byte* dst, std::uint32_t value) noexcept
{
    dst[0] = static_cast<std::byte>((value >> 24) & 0xFF);
    dst[1] = static_cast<std::byte>((value >> 16) & 0xFF);
    dst[2] = static_cast<std::byte>((value >> 8) & 0xFF);
    dst[3] = static_cast<std::byte>(value & 0xFF);
}

inline void writeUint64Be(std::byte* dst, std::uint64_t value) noexcept
{
    dst[0] = static_cast<std::byte>((value >> 56) & 0xFF);
    dst[1] = static_cast<std::byte>((value >> 48) & 0xFF);
    dst[2] = static_cast<std::byte>((value >> 40) & 0xFF);
    dst[3] = static_cast<std::byte>((value >> 32) & 0xFF);
    dst[4] = static_cast<std::byte>((value >> 24) & 0xFF);
    dst[5] = static_cast<std::byte>((value >> 16) & 0xFF);
    dst[6] = static_cast<std::byte>((value >> 8) & 0xFF);
    dst[7] = static_cast<std::byte>(value & 0xFF);
}

[[nodiscard]] inline std::uint16_t readUint16Be(const std::byte* src) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(src[0]) << 8) | static_cast<std::uint16_t>(src[1]));
}

[[nodiscard]] inline std::uint32_t readUint32Be(const std::byte* src) noexcept
{
    return (static_cast<std::uint32_t>(src[0]) << 24) | (static_cast<std::uint32_t>(src[1]) << 16) | (static_cast<std::uint32_t>(src[2]) << 8) | static_cast<std::uint32_t>(src[3]);
}

[[nodiscard]] inline std::uint64_t readUint64Be(const std::byte* src) noexcept
{
    return (static_cast<std::uint64_t>(src[0]) << 56) | (static_cast<std::uint64_t>(src[1]) << 48) | (static_cast<std::uint64_t>(src[2]) << 40) | (static_cast<std::uint64_t>(src[3]) << 32) | (static_cast<std::uint64_t>(src[4]) << 24) | (static_cast<std::uint64_t>(src[5]) << 16) | (static_cast<std::uint64_t>(src[6]) << 8) | static_cast<std::uint64_t>(src[7]);
}

} // namespace Core::Message::Wire
