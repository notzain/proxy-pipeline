#include "parsers/PugiOrderParser.hpp"

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML

#include <charconv>
#include <pugixml.hpp>
#include <string_view>

namespace Proxy::Handler::Parsers {

std::optional<Proxy::Model::OrderView> PugiOrderParser::parseDirect(Common::Compat::span<std::byte> xmlData)
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(xmlData.data(), xmlData.size(), pugi::parse_default);
    if (!result) {
        return std::nullopt;
    }

    pugi::xml_node root = doc.child("order");
    if (!root) {
        root = doc.first_child();
    }
    if (!root) {
        return std::nullopt;
    }

    auto customerNode = root.child("customer");
    auto itemNode = root.child("item");
    auto quantityNode = root.child("quantity");

    if (!customerNode || !itemNode || !quantityNode) {
        return std::nullopt;
    }

    std::string_view customerStr = customerNode.text().as_string();
    std::string_view itemStr = itemNode.text().as_string();
    std::string_view qtyStr = quantityNode.text().as_string();

    std::uint32_t qty = 0;
    auto [ptr, ec] = std::from_chars(qtyStr.data(), qtyStr.data() + qtyStr.size(), qty);
    if (ec != std::errc()) {
        return std::nullopt;
    }

    return Proxy::Model::OrderView {
        .customer = customerStr,
        .item = itemStr,
        .quantity = qty
    };
}

} // namespace Proxy::Handler::Parsers

#endif // PROXY_HAS_PUGIXML

