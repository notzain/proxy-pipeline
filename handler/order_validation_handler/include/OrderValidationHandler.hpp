#pragma once

#include "Order.hpp"
#include "core/IHandler.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Handler that parses incoming XML messages into Order models using Core::Serialization
 * and validates domain business rules for Customer, Item, and Quantity fields.
 *
 * Messages that satisfy validation criteria are forwarded to the configured output channel.
 * Messages that fail XML deserialization or semantic validation criteria are rejected.
 */
class OrderValidationHandler : public Core::IHandler {
public:
    /**
     * @brief Constructs an OrderValidationHandler with an output channel name.
     *
     * @param outputChannelName Name of the output channel to forward valid order messages to.
     */
    explicit OrderValidationHandler(std::string outputChannelName);

    ~OrderValidationHandler() override;

    /**
     * @brief Ingress callback for incoming messages.
     * Deserializes buffer to Order model, validates criteria, and forwards valid messages.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Byte buffer of the message payload.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

    /**
     * @brief Deserializes an XML buffer into an Order model.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt if deserialization fails.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrder(std::span<const std::byte> xmlData) const;

    /**
     * @brief Deserializes an XML buffer into a zero-copy OrderView model (Option 1 optimization).
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::OrderView> Parsed order view, or std::nullopt if deserialization fails.
     */
    [[nodiscard]] std::optional<Proxy::Model::OrderView> parseOrderView(std::span<const std::byte> xmlData) const;

    /**
     * @brief Validates an Order instance against domain criteria.
     *
     * Criteria:
     * - customer: non-empty, between 2 and 50 characters, letters/spaces only.
     * - item: non-empty, between 2 and 50 characters.
     * - quantity: strictly greater than 0 and at most 10,000.
     *
     * @param order The order model to validate.
     * @return true if valid according to rules, false otherwise.
     */
    [[nodiscard]] bool validateOrder(const Proxy::Model::Order& order) const;

    /**
     * @brief Validates an OrderView instance against domain criteria without string allocations.
     *
     * @param order The zero-copy order view to validate.
     * @return true if valid according to rules, false otherwise.
     */
    [[nodiscard]] bool validateOrderView(const Proxy::Model::OrderView& order) const;

private:
    std::string m_outputChannelName;
};

} // namespace Proxy::Handler
