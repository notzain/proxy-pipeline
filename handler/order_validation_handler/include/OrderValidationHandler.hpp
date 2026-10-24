#pragma once

#include "Order.hpp"
#include "core/handler/IHandler.hpp"
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
class OrderValidationHandler : public Core::Handler::IHandler {
public:
    OrderValidationHandler() = default;

    /**
     * @brief Constructs an OrderValidationHandler with an output channel name.
     *
     * @param outputChannelName Name of the output channel to forward valid order messages to.
     */
    explicit OrderValidationHandler(std::string outputChannelName);

    ~OrderValidationHandler() override;

    void setOutputChannel(::Backend::IOutputChannel* outChannel) noexcept
    {
        m_cachedOutputChannel = outChannel;
    }

    /**
     * @brief Configures OrderValidationHandler and creates input and output channels via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If configuration and channel initialization succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Ingress callback for incoming messages.
     * Deserializes buffer to Order model, validates criteria, and forwards valid messages.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Byte buffer of the message payload.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    /**
     * @brief Batch ingress callback for incoming messages.
     * Validates orders and forwards valid messages using vectorized batch transmission.
     *
     * @param channelName Name of the receiving channel.
     * @param messages Batch of enveloped message views.
     */
    void onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages) override;

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
     * @brief Deserializes a mutable XML buffer in-place into OrderView using Core::Serialization IR traits.
     *
     * @param xmlData Mutable non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::OrderView> Parsed order view, or std::nullopt if deserialization fails.
     */
    [[nodiscard]] std::optional<Proxy::Model::OrderView> parseOrderViewInPlace(std::span<std::byte> xmlData) const;

    /**
     * @brief Direct zero-copy DOM parser (Option 3 optimization).
     * Parses XML directly in-place from pugixml nodes to OrderView, bypassing the intermediate Value IR tree.
     *
     * @param xmlData Mutable non-owning span of the XML buffer for in-place parsing.
     * @return std::optional<Proxy::Model::OrderView> Parsed order view, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::OrderView> parseOrderDirect(std::span<std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order using libxml2 DOM API.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderLibXml2(std::span<const std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order using Apache Xerces-C DOM API.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderXerces(std::span<const std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order using libxml2 xmlReader streaming cursor API.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderLibXml2Reader(std::span<const std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order using Apache Xerces-C SAX2 parser.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderXercesSax(std::span<const std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order reusing a thread-local libxml2 parser context.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderLibXml2Reused(std::span<const std::byte> xmlData) const;

    /**
     * @brief Parses XML buffer to Order reusing a thread-local Xerces DOM parser instance and document pool.
     *
     * @param xmlData Non-owning span of the XML buffer.
     * @return std::optional<Proxy::Model::Order> Parsed order, or std::nullopt on syntax/mapping error.
     */
    [[nodiscard]] std::optional<Proxy::Model::Order> parseOrderXercesReused(std::span<const std::byte> xmlData) const;

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

    /**
     * @brief Fast lookup-table domain validation (Option 2 optimization).
     * Uses compile-time 256-byte bitmap tables instead of per-character standard ctype checks.
     *
     * @param order The order view to validate.
     * @return true if valid according to rules, false otherwise.
     */
    [[nodiscard]] bool validateOrderFast(const Proxy::Model::OrderView& order) const;

    /**
     * @brief Fast ingress message processing (Option 4 optimization).
     * Integrates direct in-place DOM parsing (Option 3), bitmap table validation (Option 2),
     * and cached output channel lookup to bypass map lookup and lock contention.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Mutable byte buffer of the message payload for in-place parsing.
     */
    void onMessageReceivedFast(std::string_view channelName, std::span<std::byte> buffer);

private:
    std::string m_inputChannelName;
    std::string m_outputChannelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
    ::Backend::IOutputChannel* m_cachedOutputChannel { nullptr };
};

} // namespace Proxy::Handler
