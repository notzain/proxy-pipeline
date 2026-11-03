#include "core/metadata/MetadataCatalog.hpp"
#include "core/net/UdpMetadata.hpp"

namespace Core::Metadata {

MetadataCatalog::MetadataCatalog()
{
    // Register built-in Core metadata types directly on this instance
    registerTypeInstance<Core::Net::UdpMetadata>();
}

MetadataCatalog& MetadataCatalog::instance()
{
    static MetadataCatalog catalog;
    return catalog;
}

void MetadataCatalog::registerFn(Backend::ProtocolId proto, ToIrFn fn)
{
    m_converters[proto] = std::move(fn);
}

std::optional<Serialization::Ir::Value> MetadataCatalog::toIr(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes)
{
    auto& converters = instance().m_converters;
    auto it = converters.find(proto);
    if (it != converters.end() && it->second) {
        return it->second(bytes);
    }
    return std::nullopt;
}

} // namespace Core::Metadata
