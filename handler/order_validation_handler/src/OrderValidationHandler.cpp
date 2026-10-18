#include "OrderValidationHandler.hpp"
#include "core/serialization/XmlSerializer.hpp"
#include "log/Log.hpp"

#include <algorithm>
#include <cctype>

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

} // namespace Proxy::Handler
