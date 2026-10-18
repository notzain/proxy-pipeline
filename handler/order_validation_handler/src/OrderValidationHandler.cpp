#include "OrderValidationHandler.hpp"
#include "core/serialization/XmlSerializer.hpp"
#include "log/Log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <pugixml.hpp>

namespace Proxy::Handler {

OrderValidationHandler::OrderValidationHandler(std::string outputChannelName)
    : m_outputChannelName(std::move(outputChannelName))
{
}

OrderValidationHandler::~OrderValidationHandler() = default;

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrder(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
    return Core::Serialization::XmlDeserializer<Proxy::Model::Order>::deserialize(xmlStr, { .rootName = "order" });
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderView(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
    return Core::Serialization::XmlDeserializer<Proxy::Model::OrderView>::deserialize(xmlStr, { .rootName = "order" });
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderDirect(std::span<std::byte> xmlData) const
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

bool OrderValidationHandler::validateOrder(const Proxy::Model::Order& order) const
{
    // Customer validation:
    // Must be between 2 and 50 characters, only letters, spaces, or hyphens
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        Proxy::Log::debug("[OrderValidationHandler] Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    // Must be between 2 and 50 characters, alphanumeric or spaces/hyphens/underscores
    if (order.item.size() < 2 || order.item.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        Proxy::Log::debug("[OrderValidationHandler] Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    // Must be positive and within reasonable batch limit (<= 10,000)
    if (order.quantity == 0 || order.quantity > 10000) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

bool OrderValidationHandler::validateOrderView(const Proxy::Model::OrderView& order) const
{
    // Customer validation:
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        Proxy::Log::debug("[OrderValidationHandler] Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    if (order.item.size() < 2 || order.item.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        Proxy::Log::debug("[OrderValidationHandler] Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    if (order.quantity == 0 || order.quantity > 10000) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

namespace {

    constexpr auto makeCustomerCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        return table;
    }

    constexpr auto makeItemCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = '0'; c <= '9'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        table['_'] = true;
        return table;
    }

    constexpr auto CustomerCharTable = makeCustomerCharTable();
    constexpr auto ItemCharTable = makeItemCharTable();

} // namespace

bool OrderValidationHandler::validateOrderFast(const Proxy::Model::OrderView& order) const
{
    // Customer validation: length 2..50
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        return false;
    }

    for (char ch : order.customer) {
        if (!CustomerCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Item validation: length 2..50
    if (order.item.size() < 2 || order.item.size() > 50) {
        return false;
    }

    for (char ch : order.item) {
        if (!ItemCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Quantity validation: 1..10000
    if (order.quantity == 0 || order.quantity > 10000) {
        return false;
    }

    return true;
}

void OrderValidationHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    auto order = parseOrder(buffer);
    if (!order.has_value()) {
        Proxy::Log::warn("[OrderValidationHandler] Message from '{}' ({} bytes) failed XML deserialization to Order. Discarding.",
            channelName, buffer.size());
        return;
    }

    if (!validateOrder(*order)) {
        Proxy::Log::warn("[OrderValidationHandler] Order for customer '{}' / item '{}' / qty {} failed domain validation. Discarding.",
            order->customer, order->item, order->quantity);
        return;
    }

    Proxy::Log::debug("[OrderValidationHandler] Order validated for customer '{}', item '{}', qty {}. Forwarding to '{}'...",
        order->customer, order->item, order->quantity, m_outputChannelName);

    if (!send(m_outputChannelName, buffer)) {
        Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", m_outputChannelName);
    }
}

void OrderValidationHandler::onMessageReceivedFast(std::string_view channelName, std::span<std::byte> buffer)
{
    auto order = parseOrderDirect(buffer);
    if (!order.has_value()) {
        Proxy::Log::warn("[OrderValidationHandler] Message from '{}' ({} bytes) failed XML deserialization to OrderView. Discarding.",
            channelName, buffer.size());
        return;
    }

    if (!validateOrderFast(*order)) {
        Proxy::Log::warn("[OrderValidationHandler] Order for customer '{}' / item '{}' / qty {} failed fast domain validation. Discarding.",
            order->customer, order->item, order->quantity);
        return;
    }

    if (!m_cachedOutputChannel) {
        std::shared_lock lock(m_outputMutex);
        auto it = m_outputChannels.find(m_outputChannelName);
        if (it != m_outputChannels.end()) {
            m_cachedOutputChannel = it->second.get();
        }
    }

    if (m_cachedOutputChannel) {
        if (!m_cachedOutputChannel->send(buffer)) {
            Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", m_outputChannelName);
        }
    } else {
        Proxy::Log::warn("[OrderValidationHandler] Output channel '{}' not found", m_outputChannelName);
    }
}

} // namespace Proxy::Handler
