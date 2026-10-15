#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace Core {

/**
 * @brief Simple key-value configuration store for backends and components.
 *
 * Configuration is populated by reading from a file (key=value format, with '#' or ';' for comments).
 */
class Configuration {
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
     * @brief Gets an optional string value for the given key.
     *
     * @param key Setting identifier.
     * @return std::optional<std::string> Value if present, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<std::string> get(const std::string& key) const;

    /**
     * @brief Gets a configuration value or returns a default fallback.
     *
     * @param key Setting identifier.
     * @param defaultValue Fallback value if key is not found.
     * @return std::string Value or defaultValue.
     */
    [[nodiscard]] std::string getOr(const std::string& key, const std::string& defaultValue) const;

    /**
     * @brief Checks if a key exists in the configuration.
     *
     * @param key Setting identifier.
     * @return true If present.
     * @return false Otherwise.
     */
    [[nodiscard]] bool has(const std::string& key) const noexcept;

private:
    std::unordered_map<std::string, std::string> m_entries;
};

} // namespace Core
