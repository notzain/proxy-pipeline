#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace Backend {

namespace Detail {

    template <typename T>
    std::optional<T> parseValue(std::string_view str)
    {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, std::string>) {
            return std::string(str);
        } else if constexpr (std::is_same_v<CleanT, std::string_view>) {
            return str;
        } else if constexpr (std::is_same_v<CleanT, bool>) {
            if (str == "true" || str == "1" || str == "yes" || str == "on" || str == "TRUE") {
                return true;
            }
            if (str == "false" || str == "0" || str == "no" || str == "off" || str == "FALSE") {
                return false;
            }
            return std::nullopt;
        } else if constexpr (std::is_integral_v<CleanT>) {
            CleanT val {};
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
            if (ec == std::errc()) {
                return val;
            }
            return std::nullopt;
        } else if constexpr (std::is_floating_point_v<CleanT>) {
            try {
                return static_cast<CleanT>(std::stod(std::string(str)));
            } catch (...) {
                return std::nullopt;
            }
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
