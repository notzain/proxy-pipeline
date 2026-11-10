#include "validation/OrderValidator.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <cctype>

namespace Proxy::Handler::Validation {

bool OrderValidator::validate(const Proxy::Model::Order& order)
{
    // Customer validation:
    // Must be between 2 and 50 characters, only letters, spaces, or hyphens
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        PROXY_LOG_DEBUG("Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        PROXY_LOG_DEBUG("Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    // Must be between 2 and 50 characters, alphanumeric or spaces/hyphens/underscores
    if (order.item.size() < 2 || order.item.size() > 50) {
        PROXY_LOG_DEBUG("Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        PROXY_LOG_DEBUG("Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    // Must be positive and within reasonable batch limit (<= 10,000)
    if (order.quantity == 0 || order.quantity > 10000) {
        PROXY_LOG_DEBUG("Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

bool OrderValidator::validateView(const Proxy::Model::OrderView& order)
{
    // Customer validation:
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        PROXY_LOG_DEBUG("Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        PROXY_LOG_DEBUG("Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    if (order.item.size() < 2 || order.item.size() > 50) {
        PROXY_LOG_DEBUG("Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        PROXY_LOG_DEBUG("Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    if (order.quantity == 0 || order.quantity > 10000) {
        PROXY_LOG_DEBUG("Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

bool OrderValidator::validateFast(const Proxy::Model::OrderView& order)
{
    // Customer validation: length 2..50
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        return false;
    }

    for (char ch : order.customer) {
        if (!Detail::CustomerCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Item validation: length 2..50
    if (order.item.size() < 2 || order.item.size() > 50) {
        return false;
    }

    for (char ch : order.item) {
        if (!Detail::ItemCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Quantity validation: 1..10000
    if (order.quantity == 0 || order.quantity > 10000) {
        return false;
    }

    return true;
}

} // namespace Proxy::Handler::Validation
