#pragma once

#include "backend/IConfig.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core::Config {

/**
 * @brief Key-value configuration store implementing Backend::IConfig.
 *
 * Configuration is populated by reading from a file (key=value format, with '#' or ';' for comments)
 * or by directly setting key-value pairs. Inherits strongly-typed get<T>() and getOr<T>() from IConfig.
 */
class Configuration : public Backend::IConfig {
public:
    Configuration() = default;

    /**
     * @brief Loads configuration from the specified file path.
     *
     * @param filePath Path to the configuration file.
     * @return Configuration Populated Configuration instance.
     */
    static Configuration fromFile(const std::filesystem::path& filePath);

    /**
     * @brief Sets a configuration value for a given key.
     *
     * @param key Setting identifier.
     * @param value Setting value string.
     */
    void set(std::string key, std::string value);

    /**
     * @brief Looks up a raw string value for the given key.
     *
     * @param key Setting identifier.
     * @return std::optional<std::string_view> Value view if present, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<std::string_view> getString(std::string_view key) const override;

    /**
     * @brief Checks if a key exists in the configuration.
     *
     * @param key Setting identifier.
     * @return true If present.
     * @return false Otherwise.
     */
    [[nodiscard]] bool has(std::string_view key) const noexcept override;

private:
    std::unordered_map<std::string, std::string> m_entries;
};

} // namespace Core::Config

namespace Core {
using Config::Configuration;
} // namespace Core
