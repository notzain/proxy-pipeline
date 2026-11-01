#pragma once

#include "common/memory/Borrow.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/config/IConfigItem.hpp"
#include "core/serialization/ir/Value.hpp"
#include <string>
#include <vector>

namespace Core::Config {

class Configuration;

/**
 * @brief Base class for structured configuration objects.
 *
 * ConfigSection maintains a registry of contained IConfigItem instances and provides
 * collective loading and serialization routines.
 */
class ConfigSection {
public:
    ConfigSection() = default;
    virtual ~ConfigSection() = default;

    ConfigSection(const ConfigSection&) = delete;
    ConfigSection& operator=(const ConfigSection&) = delete;
    ConfigSection(ConfigSection&&) = delete;
    ConfigSection& operator=(ConfigSection&&) = delete;

    /**
     * @brief Registers an item with this section. Automatically called by ConfigItem constructor.
     */
    void registerItem(Common::Memory::Borrow<IConfigItem> item);

    /**
     * @brief Loads all registered configuration items from the given Configuration store.
     * @return true if all items loaded successfully, false if any item encountered an error.
     */
    bool load(const Configuration& config);

    /**
     * @brief Exports all registered configuration items to an Intermediate Representation Object.
     */
    [[nodiscard]] Core::Serialization::Ir::Value toIr() const;

private:
    std::vector<Common::Memory::Borrow<IConfigItem>> m_items;
};

} // namespace Core::Config
