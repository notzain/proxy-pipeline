#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace Common::Compat {

#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L

template <typename To, typename From>
[[nodiscard]] constexpr To bit_cast(const From& src) noexcept
{
    return std::bit_cast<To>(src);
}

#else

template <typename To, typename From,
    std::enable_if_t<
        sizeof(To) == sizeof(From) && std::is_trivially_copyable_v<From> && std::is_trivially_copyable_v<To> && std::is_default_constructible_v<To>,
        int> = 0>
[[nodiscard]] To bit_cast(const From& src) noexcept
{
    To dst;
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}

#endif

} // namespace Common::Compat
