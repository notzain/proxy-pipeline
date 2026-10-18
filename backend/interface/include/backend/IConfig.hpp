#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace Backend {

namespace Detail {

    std::optional<std::string> parseString(std::string_view str);
    std::optional<std::string_view> parseStringView(std::string_view str);
    std::optional<bool> parseBool(std::string_view str);

    std::optional<int8_t> parseInt8(std::string_view str);
    std::optional<uint8_t> parseUint8(std::string_view str);
    std::optional<int16_t> parseInt16(std::string_view str);
    std::optional<uint16_t> parseUint16(std::string_view str);
    std::optional<int32_t> parseInt32(std::string_view str);
    std::optional<uint32_t> parseUint32(std::string_view str);
    std::optional<int64_t> parseInt64(std::string_view str);
    std::optional<uint64_t> parseUint64(std::string_view str);

    std::optional<float> parseFloat(std::string_view str);
    std::optional<double> parseDouble(std::string_view str);

    template <typename T>
    std::optional<T> parseValue(std::string_view str)
    {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, std::string>) {
            return parseString(str);
        } else if constexpr (std::is_same_v<CleanT, std::string_view>) {
            return parseStringView(str);
        } else if constexpr (std::is_same_v<CleanT, bool>) {
            return parseBool(str);
        } else if constexpr (std::is_same_v<CleanT, int8_t>) {
            return parseInt8(str);
        } else if constexpr (std::is_same_v<CleanT, uint8_t>) {
            return parseUint8(str);
        } else if constexpr (std::is_same_v<CleanT, int16_t>) {
            return parseInt16(str);
        } else if constexpr (std::is_same_v<CleanT, uint16_t>) {
            return parseUint16(str);
        } else if constexpr (std::is_same_v<CleanT, int32_t>) {
            return parseInt32(str);
        } else if constexpr (std::is_same_v<CleanT, uint32_t>) {
            return parseUint32(str);
        } else if constexpr (std::is_same_v<CleanT, int64_t>) {
            return parseInt64(str);
        } else if constexpr (std::is_same_v<CleanT, uint64_t>) {
            return parseUint64(str);
        } else if constexpr (std::is_same_v<CleanT, float>) {
            return parseFloat(str);
        } else if constexpr (std::is_same_v<CleanT, double>) {
            return parseDouble(str);
        } else {
            static_assert(sizeof(CleanT) == 0, "Unsupported configuration value type");
        }
    }

} // namespace Detail

/**
 * @brief Abstract key-value configuration interface for components and backends.
 */
class IConfig {
public:
    virtual ~IConfig() = default;

    /**
     * @brief Raw string lookup implemented by the underlying configuration store.
     *
     * @param key Identifier for the configuration setting.
     * @return std::optional<std::string_view> String view if key exists, std::nullopt otherwise.
     */
    [[nodiscard]] virtual std::optional<std::string_view> getString(std::string_view key) const = 0;

    /**
     * @brief Checks if a key exists in the configuration store.
     *
     * @param key Identifier for the configuration setting.
     * @return true If present.
     * @return false Otherwise.
     */
    [[nodiscard]] virtual bool has(std::string_view key) const noexcept
    {
        return getString(key).has_value();
    }

    /**
     * @brief Templated getter returning typed value if present and convertible.
     *
     * Supported types include: bool, integer types (int, uint16_t, int64_t, etc.),
     * floating-point types (float, double), std::string, and std::string_view.
     *
     * @tparam T Desired target type.
     * @param key Identifier for the configuration setting.
     * @return std::optional<T> Parsed value if present and valid, std::nullopt otherwise.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view key) const
    {
        auto raw = getString(key);
        if (!raw) {
            return std::nullopt;
        }
        return Detail::parseValue<T>(*raw);
    }

    /**
     * @brief Templated getter returning typed value or a provided fallback default.
     *
     * @tparam T Desired target type.
     * @param key Identifier for the configuration setting.
     * @param defaultValue Fallback value returned if key is missing or parsing fails.
     * @return T Parsed value or defaultValue.
     */
    template <typename T>
    [[nodiscard]] T getOr(std::string_view key, const T& defaultValue) const
    {
        return get<T>(key).value_or(defaultValue);
    }
};

} // namespace Backend
