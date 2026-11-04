#include "core/config/ConfigNodeView.hpp"
#include <charconv>

namespace Core::Config {

namespace {

    const Core::Serialization::Ir::Value* traversePath(const Core::Serialization::Ir::Value* root, std::string_view path)
    {
        if (!root || path.empty()) {
            return root;
        }

        const auto* current = root;
        size_t start = 0;

        while (start < path.size()) {
            auto dotPos = path.find('.', start);
            auto segment = (dotPos == std::string_view::npos)
                ? path.substr(start)
                : path.substr(start, dotPos - start);

            start = (dotPos == std::string_view::npos) ? path.size() : dotPos + 1;

            if (segment.empty()) {
                continue;
            }

            if (current->isObject()) {
                current = current->findField(segment);
                if (!current) {
                    return nullptr;
                }
            } else if (current->isArray()) {
                size_t index = 0;
                auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
                if (ec != std::errc()) {
                    return nullptr;
                }
                const auto* arr = current->asArray();
                if (!arr || index >= arr->size()) {
                    return nullptr;
                }
                current = &(*arr)[index];
            } else {
                return nullptr;
            }
        }

        return current;
    }

} // namespace

ConfigNodeView::ConfigNodeView(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> node)
    : m_node(node)
{
}

void ConfigNodeView::bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> node) noexcept
{
    m_node = node;
    clearCachedViews();
}

void ConfigNodeView::clearCachedViews() const noexcept
{
    m_cachedChildren.clear();
    m_cachedScalarString.clear();
}

Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> ConfigNodeView::rawValue() const noexcept
{
    return m_node;
}

std::optional<std::string_view> ConfigNodeView::getString(std::string_view key) const
{
    if (!m_node) {
        return std::nullopt;
    }

    const auto* target = traversePath(m_node.get(), key);
    if (!target) {
        return std::nullopt;
    }

    if (auto sv = target->asStringView()) {
        return sv;
    }

    if (auto b = target->asBool()) {
        return *b ? std::string_view("true") : std::string_view("false");
    }

    if (auto i = target->asInt()) {
        m_cachedScalarString = std::to_string(*i);
        return std::string_view(m_cachedScalarString);
    }

    if (auto u = target->asUInt()) {
        m_cachedScalarString = std::to_string(*u);
        return std::string_view(m_cachedScalarString);
    }

    if (auto d = target->asDouble()) {
        m_cachedScalarString = std::to_string(*d);
        return std::string_view(m_cachedScalarString);
    }

    return std::nullopt;
}

bool ConfigNodeView::has(std::string_view key) const noexcept
{
    if (!m_node) {
        return false;
    }
    return traversePath(m_node.get(), key) != nullptr;
}

Common::Memory::ObserverPtr<const Backend::IConfig> ConfigNodeView::getNode(std::string_view path) const
{
    if (!m_node) {
        return nullptr;
    }

    const auto* target = traversePath(m_node.get(), path);
    if (!target) {
        return nullptr;
    }

    auto view = std::make_unique<ConfigNodeView>(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(target));
    auto* rawPtr = view.get();
    m_cachedChildren.push_back(std::move(view));
    return Common::Memory::ObserverPtr<const Backend::IConfig>(rawPtr);
}

std::vector<Common::Memory::ObserverPtr<const Backend::IConfig>> ConfigNodeView::getChildren(std::string_view path) const
{
    std::vector<Common::Memory::ObserverPtr<const Backend::IConfig>> result;
    if (!m_node) {
        return result;
    }

    const auto* target = path.empty() ? m_node.get() : traversePath(m_node.get(), path);
    if (!target) {
        return result;
    }

    if (auto* arr = target->asArray()) {
        result.reserve(arr->size());
        for (const auto& elem : *arr) {
            auto view = std::make_unique<ConfigNodeView>(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&elem));
            result.push_back(Common::Memory::ObserverPtr<const Backend::IConfig>(view.get()));
            m_cachedChildren.push_back(std::move(view));
        }
    } else if (auto* obj = target->asObject()) {
        result.reserve(obj->size());
        for (const auto& [k, v] : *obj) {
            auto view = std::make_unique<ConfigNodeView>(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&v));
            result.push_back(Common::Memory::ObserverPtr<const Backend::IConfig>(view.get()));
            m_cachedChildren.push_back(std::move(view));
        }
    }

    return result;
}

} // namespace Core::Config
