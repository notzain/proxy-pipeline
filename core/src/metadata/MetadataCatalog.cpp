#include "core/metadata/MetadataCatalog.hpp"
#include "core/net/UdpMetadata.hpp"
#include "core/serialization/json/JsonFormat.hpp"

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

void MetadataCatalog::registerFieldExtractorFn(Backend::ProtocolId proto, FieldExtractorFn fn)
{
    m_fieldExtractors[proto] = std::move(fn);
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

std::optional<Serialization::Ir::Value> MetadataCatalog::getField(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes, std::string_view fieldName)
{
    auto& extractors = instance().m_fieldExtractors;
    auto it = extractors.find(proto);
    if (it != extractors.end() && it->second) {
        return it->second(bytes, fieldName);
    }

    // Fallback: convert to IR object and look up field
    auto ir = toIr(proto, bytes);
    if (!ir || !ir->isObject()) {
        return std::nullopt;
    }
    const auto* field = ir->findField(fieldName);
    if (!field) {
        return std::nullopt;
    }
    return *field;
}

std::string MetadataCatalog::formatJson(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes, bool pretty)
{
    auto ir = toIr(proto, bytes);
    if (!ir) {
        return "{}";
    }
    Serialization::Json::JsonOptions opts;
    opts.indent = pretty ? 2 : -1;
    return Serialization::Json::JsonFormat::encode(*ir, opts);
}

} // namespace Core::Metadata
