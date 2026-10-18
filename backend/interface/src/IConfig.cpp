#include "backend/IConfig.hpp"
#include <charconv>

namespace Backend::Detail {

std::optional<std::string> parseString(std::string_view str)
{
    return std::string(str);
}

std::optional<std::string_view> parseStringView(std::string_view str)
{
    return str;
}

std::optional<bool> parseBool(std::string_view str)
{
    if (str == "true" || str == "1" || str == "yes" || str == "on" || str == "TRUE") {
        return true;
    }
    if (str == "false" || str == "0" || str == "no" || str == "off" || str == "FALSE") {
        return false;
    }
    return std::nullopt;
}

namespace {
    template <typename IntT>
    std::optional<IntT> parseInt(std::string_view str)
    {
        IntT val { };
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
        if (ec == std::errc()) {
            return val;
        }
        return std::nullopt;
    }
} // namespace

std::optional<int8_t> parseInt8(std::string_view str)
{
    return parseInt<int8_t>(str);
}

std::optional<uint8_t> parseUint8(std::string_view str)
{
    return parseInt<uint8_t>(str);
}

std::optional<int16_t> parseInt16(std::string_view str)
{
    return parseInt<int16_t>(str);
}

std::optional<uint16_t> parseUint16(std::string_view str)
{
    return parseInt<uint16_t>(str);
}

std::optional<int32_t> parseInt32(std::string_view str)
{
    return parseInt<int32_t>(str);
}

std::optional<uint32_t> parseUint32(std::string_view str)
{
    return parseInt<uint32_t>(str);
}

std::optional<int64_t> parseInt64(std::string_view str)
{
    return parseInt<int64_t>(str);
}

std::optional<uint64_t> parseUint64(std::string_view str)
{
    return parseInt<uint64_t>(str);
}

std::optional<float> parseFloat(std::string_view str)
{
    try {
        return std::stof(std::string(str));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDouble(std::string_view str)
{
    try {
        return std::stod(std::string(str));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace Backend::Detail
