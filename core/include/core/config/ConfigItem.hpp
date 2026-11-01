#pragma once

#include "common/memory/ObserverPtr.hpp"
#include "core/config/ConfigSection.hpp"
#include "core/config/Configuration.hpp"
#include "core/config/IConfigItem.hpp"
#include "core/serialization/ir/Converter.hpp"
#include <fmt/format.h>
#include <string_view>
#include <utility>

namespace Core::Config {

/**
 * @brief Strongly-typed configuration item bound to a ConfigSection.
 *
 * @tparam T Underlying configuration value type (e.g. std::string, int, uint16_t, bool, double).
 */
template <typename T>
class ConfigItem final : public IConfigItem {
public:
    /**
     * @brief Constructs a ConfigItem, registering it with the parent ConfigSection.
     *
     * @param section Optional observer pointer to enclosing ConfigSection (typically `this`).
     * @param keyName Dotted configuration key name (e.g. "egress.dest_port").
     * @param defaultValue Default value used if key is omitted or invalid.
     */
    ConfigItem(Common::Memory::ObserverPtr<ConfigSection> section, std::string_view keyName, T defaultValue)
        : m_key(keyName)
        , m_value(std::move(defaultValue))
    {
        if (section) {
            section->registerItem(*this);
        }
    }

    ~ConfigItem() override = default;

    ConfigItem(const ConfigItem&) = delete;
    ConfigItem& operator=(const ConfigItem&) = delete;
    ConfigItem(ConfigItem&&) = delete;
    ConfigItem& operator=(ConfigItem&&) = delete;

    [[nodiscard]] std::string_view key() const noexcept override
    {
        return m_key;
    }

    [[nodiscard]] const T& value() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] operator const T&() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] operator T&() noexcept
    {
        return m_value;
    }

    [[nodiscard]] const T* operator->() const noexcept
    {
        return &m_value;
    }

    [[nodiscard]] T* operator->() noexcept
    {
        return &m_value;
    }

    template <typename U>
    [[nodiscard]] bool operator==(const U& other) const noexcept
    {
        return m_value == other;
    }

    template <typename U>
    [[nodiscard]] bool operator!=(const U& other) const noexcept
    {
        return m_value != other;
    }

    bool loadFrom(const Configuration& config) override
    {
        if (config.has(m_key)) {
            auto val = config.get<T>(m_key);
            if (val.has_value()) {
                m_value = std::move(*val);
                return true;
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] Core::Serialization::Ir::Value toIr() const override
    {
        return Core::Serialization::Ir::toIntermediate(m_value);
    }

private:
    std::string_view m_key;
    T m_value;
};

} // namespace Core::Config

template <typename T>
struct fmt::formatter<Core::Config::ConfigItem<T>> : fmt::formatter<T> {
    auto format(const Core::Config::ConfigItem<T>& item, fmt::format_context& ctx) const
    {
        return fmt::formatter<T>::format(item.value(), ctx);
    }
};
