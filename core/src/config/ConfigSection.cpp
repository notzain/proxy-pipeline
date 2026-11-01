#include "core/config/ConfigSection.hpp"
#include <utility>

namespace Core::Config {

void ConfigSection::registerItem(Common::Memory::Borrow<IConfigItem> item)
{
    m_items.push_back(item);
}

bool ConfigSection::load(const Configuration& config)
{
    bool allOk = true;
    for (auto& item : m_items) {
        if (!item->loadFrom(config)) {
            allOk = false;
        }
    }
    return allOk;
}

Core::Serialization::Ir::Value ConfigSection::toIr() const
{
    Core::Serialization::Ir::ObjectType obj;
    for (const auto& item : m_items) {
        obj[std::string(item->key())] = item->toIr();
    }
    return Core::Serialization::Ir::Value(std::move(obj));
}

} // namespace Core::Config
