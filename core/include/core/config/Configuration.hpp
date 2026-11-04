#pragma once

#include "backend/IConfig.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/config/ConfigNodeView.hpp"
#include "core/serialization/ir/Value.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Config {

/**
 * @brief Owning hierarchical configuration store implementing Backend::IConfig backed by Ir::Value.
 */
class Configuration : public ConfigNodeView {
public:
    Configuration();
    explicit Configuration(Core::Serialization::Ir::Value root);
    ~Configuration() override = default;

    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
    Configuration(Configuration&& other) noexcept;
    Configuration& operator=(Configuration&& other) noexcept;

    /**
     * @brief Loads configuration from the specified JSON file path.
     *
     * @param filePath Path to the JSON configuration file.
     * @return Configuration Populated Configuration instance.
     */
    static Configuration fromFile(const std::filesystem::path& filePath);

    /**
     * @brief Alias for fromFile.
     */
    static Configuration fromJsonFile(const std::filesystem::path& filePath);

    /**
     * @brief Parses configuration from a JSON string.
     *
     * @param jsonStr JSON input string.
     * @return Configuration Populated Configuration instance.
     */
    static Configuration fromJson(std::string_view jsonStr);

    /**
     * @brief Sets a configuration string value for a given dot-separated path.
     *
     * Creates nested intermediate objects as needed.
     *
     * @param key Dot-separated setting identifier (e.g. "server.port").
     * @param value Setting value string.
     */
    void set(std::string key, std::string value);

    /**
     * @brief Returns non-owning observer pointer to the root Ir::Value.
     */
    [[nodiscard]] Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> rawRoot() const noexcept;

private:
    Core::Serialization::Ir::Value m_root;
};

} // namespace Core::Config
