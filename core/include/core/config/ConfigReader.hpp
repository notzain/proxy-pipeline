#pragma once

#include "backend/IConfig.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/config/ConfigNodeView.hpp"
#include "core/config/Configuration.hpp"
#include "core/serialization/TypeTraits.hpp"
#include "core/serialization/ir/Converter.hpp"
#include "core/serialization/ir/Value.hpp"
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Core::Config {

namespace Detail {

    template <typename T>
    struct IsVector : std::false_type { };

    template <typename T, typename Alloc>
    struct IsVector<std::vector<T, Alloc>> : std::true_type { };

    template <typename T>
    inline constexpr bool IsVectorV = IsVector<Common::Compat::remove_cvref_t<T>>::value;

    inline Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> extractIr(const Backend::IConfig& config)
    {
        if (const auto* cfg = dynamic_cast<const Configuration*>(&config)) {
            return cfg->rawRoot();
        }
        if (const auto* nodeView = dynamic_cast<const ConfigNodeView*>(&config)) {
            return nodeView->rawValue();
        }
        return nullptr;
    }

} // namespace Detail

/**
 * @brief Retrieves a typed value from an IConfig instance, seamlessly supporting
 * reflected POD structs (via TypeTraits<T>), std::vector<T>, or primitive scalars.
 *
 * @tparam T Target type to extract/deserialize.
 * @param config The configuration store or node view.
 * @param path Dot-separated path to the configuration property (empty string for root).
 * @return std::optional<T> Deserialized or parsed value if successful, std::nullopt otherwise.
 */
template <typename T>
std::optional<T> get(const Backend::IConfig& config, std::string_view path = "")
{
    using CleanT = Common::Compat::remove_cvref_t<T>;

    if constexpr (Core::Serialization::TypeTraits<CleanT>::isReflected || Detail::IsVectorV<CleanT>) {
        auto ir = Detail::extractIr(config);
        if (!ir) {
            return std::nullopt;
        }

        const auto* targetNode = ir.get();
        if (!path.empty()) {
            auto subNode = config.getNode(path);
            if (!subNode) {
                return std::nullopt;
            }
            auto subIr = Detail::extractIr(*subNode);
            if (!subIr) {
                return std::nullopt;
            }
            targetNode = subIr.get();
        }

        return Core::Serialization::Ir::fromIntermediate<CleanT>(*targetNode);
    } else {
        return config.template get<CleanT>(path);
    }
}

/**
 * @brief Retrieves a typed value from an IConfig instance, returning fallback default if absent.
 */
template <typename T>
T getOr(const Backend::IConfig& config, std::string_view path, const T& defaultValue)
{
    return get<T>(config, path).value_or(defaultValue);
}

} // namespace Core::Config
