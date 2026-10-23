#include "XmlVerificationHandler.hpp"
#include "XmlSchemaValidator.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/memory/SmallSpanBuffer.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"

namespace Proxy::Handler {

XmlVerificationHandler::XmlVerificationHandler() = default;

XmlVerificationHandler::XmlVerificationHandler(std::filesystem::path schemaPath, std::string outputChannelName)
    : m_schemaPath(std::move(schemaPath))
    , m_outputChannelName(std::move(outputChannelName))
    , m_validator(std::make_unique<XmlSchemaValidator>(m_schemaPath))
{
}

XmlVerificationHandler::~XmlVerificationHandler() = default;

bool XmlVerificationHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
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

    m_inputChannelName = config.getOr<std::string>("xml.input_channel", config.getOr<std::string>("channel.input", "xml_in"));
    m_outputChannelName = config.getOr<std::string>("xml.output_channel", config.getOr<std::string>("channel.output", "xml_out"));

    auto inChannel = backend.createInputChannel(m_inputChannelName, config);
    if (!inChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create input channel '{}'", m_inputChannelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_inputChannelName, std::move(inChannel));

    auto outChannel = backend.createOutputChannel(m_outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create output channel '{}'", m_outputChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(m_outputChannelName, std::move(outChannel));

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

void XmlVerificationHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    if (validate(message.payload())) {
        Proxy::Log::debug("[XmlVerificationHandler] Message from '{}' ({} bytes payload) verified against XSD schema. Forwarding to '{}'...",
            channelName, message.payload().size(), m_outputChannelName);
        if (m_outputChannel) {
            if (!m_outputChannel->send(message.rawBuffer())) {
                Proxy::Log::warn("[XmlVerificationHandler] Failed to forward validated message to '{}'", m_outputChannelName);
            }
        } else {
            Proxy::Log::warn("[XmlVerificationHandler] Output channel '{}' not found", m_outputChannelName);
        }
    } else {
        Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes payload) REJECTED by XSD schema '{}'. Discarding message.",
            channelName, message.payload().size(), m_schemaPath.string());
    }
}

void XmlVerificationHandler::onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages)
{
    if (messages.empty()) {
        return;
    }

    Core::Memory::SmallSpanBuffer spans(messages.size());
    std::size_t validCount = 0;

    for (const auto& msg : messages) {
        if (validate(msg.payload())) {
            spans[validCount++] = msg.rawBuffer();
        } else {
            Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes payload) REJECTED by XSD schema '{}'. Discarding message.",
                channelName, msg.payload().size(), m_schemaPath.string());
        }
    }

    if (validCount > 0) {
        if (m_outputChannel) {
            m_outputChannel->sendBatch(spans.span(validCount));
        } else {
            Proxy::Log::warn("[XmlVerificationHandler] Output channel '{}' not found for sendBatch", m_outputChannelName);
        }
    }
}

} // namespace Proxy::Handler
