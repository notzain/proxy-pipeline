#include "core/config/Configuration.hpp"
#include "core/serialization/json/JsonFormat.hpp"
#include <fstream>
#include <sstream>

namespace Core::Config {

Configuration::Configuration()
    : ConfigNodeView()
    , m_root(Core::Serialization::Ir::ObjectType { })
{
    bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root));
}

Configuration::Configuration(Core::Serialization::Ir::Value root)
    : ConfigNodeView()
    , m_root(std::move(root))
{
    bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root));
}

Configuration::Configuration(Configuration&& other) noexcept
    : ConfigNodeView(std::move(other))
    , m_root(std::move(other.m_root))
{
    bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root));
}

Configuration& Configuration::operator=(Configuration&& other) noexcept
{
    if (this != &other) {
        ConfigNodeView::operator=(std::move(other));
        m_root = std::move(other.m_root);
        bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root));
    }
    return *this;
}

Configuration Configuration::fromJson(std::string_view jsonStr)
{
    auto decoded = Core::Serialization::Json::JsonFormat::decode(jsonStr);
    if (!decoded) {
        return Configuration { };
    }
    return Configuration(std::move(*decoded));
}

Configuration Configuration::fromJsonFile(const std::filesystem::path& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return Configuration { };
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return fromJson(buffer.str());
}

Configuration Configuration::fromFile(const std::filesystem::path& filePath)
{
    return fromJsonFile(filePath);
}

void Configuration::set(std::string key, std::string value)
{
    clearCachedViews();

    if (!m_root.isObject()) {
        m_root = Core::Serialization::Ir::ObjectType { };
        bindNode(Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root));
    }

    auto* current = &m_root;
    std::string_view path(key);
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

        if (start >= path.size()) {
            // Leaf segment
            current->setField(std::string(segment), Core::Serialization::Ir::Value(std::move(value)));
            break;
        }

        // Intermediate segment
        auto* next = current->findField(segment);
        if (!next || !next->isObject()) {
            current->setField(std::string(segment), Core::Serialization::Ir::ObjectType { });
            next = current->findField(segment);
        }
        current = const_cast<Core::Serialization::Ir::Value*>(next);
    }
}

Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value> Configuration::rawRoot() const noexcept
{
    return Common::Memory::ObserverPtr<const Core::Serialization::Ir::Value>(&m_root);
}

} // namespace Core::Config
