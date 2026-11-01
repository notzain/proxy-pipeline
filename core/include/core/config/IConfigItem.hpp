#pragma once

#include "core/serialization/ir/Value.hpp"
#include <string_view>

namespace Core::Config {

class Configuration;

/**
 * @brief Abstract interface representing a strongly-typed configuration item.
 */
class IConfigItem {
public:
    virtual ~IConfigItem() = default;

    /**
     * @brief Gets the configuration key name (e.g. "egress.dest_port").
     */
    [[nodiscard]] virtual std::string_view key() const noexcept = 0;

    /**
     * @brief Loads the value from the provided Configuration store.
     * @return true if loaded or successfully fell back to default, false on type mismatch error.
     */
    virtual bool loadFrom(const Configuration& config) = 0;

    /**
     * @brief Converts the current value to an intermediate representation value.
     */
    [[nodiscard]] virtual Core::Serialization::Ir::Value toIr() const = 0;
};

} // namespace Core::Config

