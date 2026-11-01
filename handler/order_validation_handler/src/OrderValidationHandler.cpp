#include "OrderValidationHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/MessageView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include "parsers/LibXml2OrderParser.hpp"
#include "parsers/PugiOrderParser.hpp"
#include "parsers/XercesOrderParser.hpp"
#include "validation/OrderValidator.hpp"

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
#include "core/serialization/xml/XmlSerializer.hpp"
#elif defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
#include "core/serialization/xml/XercesSerializer.hpp"
#endif

#include <algorithm>
#include <string_view>

namespace Proxy::Handler {

OrderValidationHandler::~OrderValidationHandler() = default;

bool OrderValidationHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto inputChannelName = config.getOr<std::string>("order.input_channel", "order_in");
    const auto outputChannelName = config.getOr<std::string>("order.output_channel", "order_out");

    auto inChannel = backend().createInputChannel(inputChannelName, config);
    if (!inChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create input channel '{}'", inputChannelName);
        return false;
    }
    registerInputChannel(std::move(inChannel));

    auto outChannel = backend().createOutputChannel(outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create output channel '{}'", outputChannelName);
        return false;
    }
    m_cachedOutputChannel = registerOutputChannel(std::move(outChannel));

    return true;
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrder(Common::Compat::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::Order>::deserialize(xmlStr, { .rootName = "order" });
#elif defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
    return Core::Serialization::Xml::XercesDeserializer<Proxy::Model::Order>::deserialize(xmlStr, { .rootName = "order" });
#elif defined(PROXY_HAS_LIBXML2) && PROXY_HAS_LIBXML2
    return parseOrderLibXml2Reused(xmlData);
#else
    return std::nullopt;
#endif
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderView(Common::Compat::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::OrderView>::deserialize(xmlStr, { .rootName = "order" });
#else
    (void)xmlStr;
    return std::nullopt;
#endif
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderViewInPlace(Common::Compat::span<std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::OrderView>::deserializeInPlace(
        xmlData.data(),
        xmlData.size(),
        { .rootName = "order" });
#else
    (void)xmlData;
    return std::nullopt;
#endif
}

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderDirect(Common::Compat::span<std::byte> xmlData) const
{
    return Parsers::PugiOrderParser::parseDirect(xmlData);
}
#endif

#if defined(PROXY_HAS_LIBXML2) && PROXY_HAS_LIBXML2
std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::LibXml2OrderParser::parseDom(xmlData);
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2Reader(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::LibXml2OrderParser::parseReader(xmlData);
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2Reused(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::LibXml2OrderParser::parseReused(xmlData);
}
#endif

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXerces(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::XercesOrderParser::parseDom(xmlData);
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXercesSax(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::XercesOrderParser::parseSax(xmlData);
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXercesReused(Common::Compat::span<const std::byte> xmlData) const
{
    return Parsers::XercesOrderParser::parseReused(xmlData);
}
#endif

bool OrderValidationHandler::validateOrder(const Proxy::Model::Order& order) const
{
    return Validation::OrderValidator::validate(order);
}

bool OrderValidationHandler::validateOrderView(const Proxy::Model::OrderView& order) const
{
    return Validation::OrderValidator::validateView(order);
}

bool OrderValidationHandler::validateOrderFast(const Proxy::Model::OrderView& order) const
{
    return Validation::OrderValidator::validateFast(order);
}

Backend::MessageResult OrderValidationHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
{
    auto order = parseOrder(message.payload());
    if (!order.has_value()) {
        Proxy::Log::warn("[OrderValidationHandler] Message from '{}' ({} bytes) failed XML deserialization to Order. Discarding.",
            channelName, message.payload().size());
        return Backend::Dropped {
            .reason = "Failed XML deserialization to Order",
            .errorCode = 400
        };
    }

    if (!validateOrder(*order)) {
        Proxy::Log::warn("[OrderValidationHandler] Order for customer '{}' / item '{}' / qty {} failed domain validation. Discarding.",
            order->customer, order->item, order->quantity);
        return Backend::Dropped {
            .reason = "Failed domain business rules validation (customer/item/qty)",
            .errorCode = 422
        };
    }

    if (!m_cachedOutputChannel) {
        Proxy::Log::warn("[OrderValidationHandler] No output channel is available for forwarding");
        return Backend::Dropped {
            .reason = "No output channel available for forwarding",
            .errorCode = 500
        };
    }

    const auto outputChannelName = m_cachedOutputChannel->name();
    Proxy::Log::debug("[OrderValidationHandler] Order validated for customer '{}', item '{}', qty {}. Forwarding to '{}'...",
        order->customer, order->item, order->quantity, outputChannelName);
    if (!m_cachedOutputChannel->send(message)) {
        Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", outputChannelName);
    }
    return Backend::Forwarded { .channel = outputChannelName };
}

void OrderValidationHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    if (messages.empty())
        return;

    Common::Memory::SmallSpanBuffer<Backend::MessageView, 64> validMessages(messages.size());
    std::size_t validCount = 0;
    const auto count = std::min(messages.size(), outResults.size());

    for (std::size_t i = 0; i < count; ++i) {
        const auto& msg = messages[i];
        auto order = parseOrder(msg.payload());
        if (!order.has_value()) {
            Proxy::Log::warn("[OrderValidationHandler] Message from '{}' ({} bytes) failed XML deserialization. Discarding.",
                channelName, msg.payload().size());
            outResults[i] = Backend::Dropped {
                .reason = "Failed XML deserialization to Order",
                .errorCode = 400
            };
            continue;
        }

        if (!validateOrder(*order)) {
            Proxy::Log::warn("[OrderValidationHandler] Order for customer '{}' / item '{}' / qty {} failed domain validation. Discarding.",
                order->customer, order->item, order->quantity);
            outResults[i] = Backend::Dropped {
                .reason = "Failed domain business rules validation (customer/item/qty)",
                .errorCode = 422
            };
            continue;
        }

        if (m_cachedOutputChannel) {
            validMessages[validCount++] = msg;
            outResults[i] = Backend::Forwarded { .channel = m_cachedOutputChannel->name() };
        } else {
            outResults[i] = Backend::Dropped {
                .reason = "No output channel available for forwarding",
                .errorCode = 500
            };
        }
    }

    if (validCount == 0) {
        return;
    }

    if (m_cachedOutputChannel) {
        m_cachedOutputChannel->sendBatch(validMessages.span(validCount));
    }
}

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
void OrderValidationHandler::onMessageReceivedFast(std::string_view channelName, Common::Compat::span<std::byte> buffer)
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

    if (m_cachedOutputChannel) {
        if (!m_cachedOutputChannel->send(buffer)) {
            Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", m_cachedOutputChannel->name());
        }
    } else {
        Proxy::Log::warn("[OrderValidationHandler] No output channel is available for forwarding");
    }
}
#endif

} // namespace Proxy::Handler
