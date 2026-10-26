#include "XmlVerificationHandler.hpp"
#include "XmlSchemaValidator.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/memory/SmallSpanBuffer.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"

namespace Proxy::Handler {

XmlVerificationHandler::XmlVerificationHandler(::Backend::IBackend& backend)
    : Core::Handler::IHandler(backend)
{
}

XmlVerificationHandler::XmlVerificationHandler(
    ::Backend::IBackend& backend,
    std::filesystem::path schemaPath)
    : Core::Handler::IHandler(backend)
    , m_schemaPath(std::move(schemaPath))
    , m_validator(std::make_unique<XmlSchemaValidator>(m_schemaPath))
{
}

XmlVerificationHandler::~XmlVerificationHandler() = default;

bool XmlVerificationHandler::onConfigure(const Core::Config::Configuration& config)
{
    std::string xsdPath = config.getOr<std::string>("xml.xsd_path", "");
    if (xsdPath.empty()) {
        Proxy::Log::error("[XmlVerificationHandler] Missing mandatory configuration: 'xml.xsd_path'");
        return false;
    }
    m_schemaPath = xsdPath;
    m_validator = std::make_unique<XmlSchemaValidator>(m_schemaPath);
    if (!isSchemaLoaded()) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to load XSD schema from '{}'.", xsdPath);
        return false;
    }

    const auto inputChannelName = config.getOr<std::string>("xml.input_channel", "xml_in");
    const auto outputChannelName = config.getOr<std::string>("xml.output_channel", "xml_out");

    auto inChannel = backend().createInputChannel(inputChannelName, config);
    if (!inChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create input channel '{}'", inputChannelName);
        return false;
    }
    registerInputChannel(std::move(inChannel));

    auto outChannel = backend().createOutputChannel(outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create output channel '{}'", outputChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(std::move(outChannel));

    return true;
}

bool XmlVerificationHandler::isSchemaLoaded() const noexcept
{
    return m_validator && m_validator->isLoaded();
}

bool XmlVerificationHandler::validate(std::span<const std::byte> xmlData) const
{
    return m_validator && m_validator->validate(xmlData);
}

Core::Message::MessageResult XmlVerificationHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    if (validate(message.payload())) {
        if (!m_outputChannel) {
            Proxy::Log::warn("[XmlVerificationHandler] No output channel is available for forwarding");
            return Core::Message::Dropped {
                .reason = "No output channel available for forwarding",
                .errorCode = 500
            };
        }
        const auto outputChannelName = m_outputChannel->name();
        Proxy::Log::debug("[XmlVerificationHandler] Message from '{}' ({} bytes payload) verified against XSD schema. Forwarding to '{}'...",
            channelName, message.payload().size(), outputChannelName);
        if (!m_outputChannel->send(message.rawBuffer())) {
            Proxy::Log::warn("[XmlVerificationHandler] Failed to forward validated message to '{}'", outputChannelName);
        }
        return Core::Message::Forwarded { .channel = outputChannelName };
    }

    Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes payload) REJECTED by XSD schema '{}'. Discarding message.",
        channelName, message.payload().size(), m_schemaPath.string());
    return Core::Message::Dropped {
        .reason = "Rejected: XSD schema validation failure",
        .errorCode = 400
    };
}

void XmlVerificationHandler::onBatchReceived(
    std::string_view channelName,
    std::span<const Core::Message::MessageView> messages,
    std::span<Core::Message::MessageResult> outResults)
{
    if (messages.empty())
        return;

    Core::Memory::SmallSpanBuffer spans(messages.size());
    std::size_t validCount = 0;
    const auto count = std::min(messages.size(), outResults.size());

    for (std::size_t i = 0; i < count; ++i) {
        const auto& msg = messages[i];
        if (validate(msg.payload())) {
            if (m_outputChannel) {
                spans[validCount++] = msg.rawBuffer();
                outResults[i] = Core::Message::Forwarded { .channel = m_outputChannel->name() };
            } else {
                outResults[i] = Core::Message::Dropped {
                    .reason = "No output channel available for forwarding",
                    .errorCode = 500
                };
            }
        } else {
            Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes payload) REJECTED by XSD schema '{}'. Discarding message.",
                channelName, msg.payload().size(), m_schemaPath.string());
            outResults[i] = Core::Message::Dropped {
                .reason = "Rejected: XSD schema validation failure",
                .errorCode = 400
            };
        }
    }

    if (validCount > 0) {
        m_outputChannel->sendBatch(spans.span(validCount));
    }
}

} // namespace Proxy::Handler
