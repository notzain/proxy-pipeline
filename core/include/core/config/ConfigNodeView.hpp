#pragma once

#include "backend/IConfig.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/serialization/ir/Value.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Config {

/**
 * @brief Base class or non-owning view providing dot-notation IConfig navigation over Ir::Value.
 */
class ConfigNodeView : public Backend::IConfig {
public:
    explicit ConfigNodeView(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> node = nullptr);
    ~ConfigNodeView() override = default;

    ConfigNodeView(const ConfigNodeView&) = delete;
    ConfigNodeView& operator=(const ConfigNodeView&) = delete;
    ConfigNodeView(ConfigNodeView&&) noexcept = default;
    ConfigNodeView& operator=(ConfigNodeView&&) noexcept = default;

    [[nodiscard]] std::optional<std::string_view> getString(std::string_view key) const override;
    [[nodiscard]] bool has(std::string_view key) const noexcept override;

    [[nodiscard]] Common::Memory::ObserverPtr<const Backend::IConfig> getNode(std::string_view path) const override;
    [[nodiscard]] std::vector<Common::Memory::ObserverPtr<const Backend::IConfig>> getChildren(std::string_view path = "") const override;

    [[nodiscard]] Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> rawValue() const noexcept;

protected:
    void bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> node) noexcept;
    void clearCachedViews() const noexcept;

private:
    Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> m_node;
    mutable std::string m_cachedScalarString;
    mutable std::vector<std::unique_ptr<ConfigNodeView>> m_cachedChildren;
};

} // namespace Core::Config
