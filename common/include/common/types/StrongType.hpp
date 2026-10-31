#pragma once

#include <type_traits>
#include <utility>

namespace Common::Types {

/**
 * @brief Zero-overhead templated strong type wrapper around an underlying value type `T`.
 *
 * Prevents accidental implicit conversions between unrelated domain identifiers while preserving
 * `constexpr` evaluation, standard layout (`sizeof(StrongType<T, Tag>) == sizeof(T)`), and
 * equality/ordering comparisons.
 *
 * @tparam T Underlying value type (e.g. `std::uint8_t`).
 * @tparam Tag Phantom tag type distinguishing this strong type from others with the same `T`.
 */
template <typename T, typename Tag>
class StrongType {
public:
    using value_type = T;

    constexpr StrongType() noexcept(std::is_nothrow_default_constructible_v<T>)
        : m_value { }
    {
    }

    constexpr explicit StrongType(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : m_value(std::move(value))
    {
    }

    [[nodiscard]] constexpr const T& value() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] constexpr T& value() noexcept
    {
        return m_value;
    }

    [[nodiscard]] constexpr explicit operator T() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] friend constexpr bool operator==(const StrongType& lhs, const StrongType& rhs) noexcept
    {
        return lhs.m_value == rhs.m_value;
    }

    [[nodiscard]] friend constexpr bool operator!=(const StrongType& lhs, const StrongType& rhs) noexcept
    {
        return lhs.m_value != rhs.m_value;
    }

    [[nodiscard]] friend constexpr bool operator<(const StrongType& lhs, const StrongType& rhs) noexcept
    {
        return lhs.m_value < rhs.m_value;
    }

private:
    T m_value;
};

} // namespace Common::Types
