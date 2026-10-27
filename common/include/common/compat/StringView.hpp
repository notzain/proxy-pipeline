#pragma once

#include <string_view>

namespace Common::Compat {

[[nodiscard]] constexpr bool starts_with(std::string_view str, std::string_view prefix) noexcept
{
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] constexpr bool starts_with(std::string_view str, char c) noexcept
{
    return !str.empty() && str.front() == c;
}

[[nodiscard]] constexpr bool ends_with(std::string_view str, std::string_view suffix) noexcept
{
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
}

[[nodiscard]] constexpr bool ends_with(std::string_view str, char c) noexcept
{
    return !str.empty() && str.back() == c;
}

} // namespace Common::Compat
