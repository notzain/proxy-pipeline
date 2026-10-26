#pragma once

#include "core/handler/IHandler.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Forward declaration of internal schema validator implementation (PIMPL pattern)
 * to keep concrete libxml2 headers out of the public header.
 */
class XmlSchemaValidator;

/**
 * @brief Handler that validates incoming XML messages against an XSD schema.
 *
 * Messages that satisfy the schema are forwarded to the configured output channel.
 * Messages that fail schema validation or contain malformed XML are rejected/discarded.
 */
class XmlVerificationHandler : public Core::Handler::IHandler {
public:
    explicit XmlVerificationHandler(::Backend::IBackend& backend);

    /**
     * @brief Constructs an XmlVerificationHandler with a backend and schema file.
     *
     * @param backend Backend used to create channels and report message results.
     * @param schemaPath Path to the .xsd schema file.
     */
    XmlVerificationHandler(::Backend::IBackend& backend, std::filesystem::path schemaPath);

    ~XmlVerificationHandler() override;

    /**
     * @brief Configures XmlVerificationHandler, loads schema from 'xml.xsd_path',
     * and creates input and output channels via the backend.
     *
     * @param config Configuration store.
     * @return true If configuration, schema loading, and channel initialization succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    /**
     * @brief Ingress callback for incoming enveloped messages.
     * Validates message.payload() against schema and forwards full message to output channel if valid.
     *
     * @param channelName Name of the channel that received the message.
     * @param message Validated view of the received enveloped message.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    /**
     * @brief Vectorized ingress callback populating per-message outcomes.
     *
     * @param channelName Name of the channel that received the messages.
     * @param messages Vectorized view of message views.
     * @param outResults Output span populated with per-message processing outcomes in input order.
     */
    void onBatchReceived(
        std::string_view channelName,
        std::span<const Core::Message::MessageView> messages,
        std::span<Core::Message::MessageResult> outResults) override;

    /**
     * @brief Validates an XML byte buffer against the loaded schema.
     *
     * @param xmlData Non-owning span of the XML document.
     * @return true If valid according to the schema, false otherwise.
     */
    [[nodiscard]] bool validate(std::span<const std::byte> xmlData) const;

    /**
     * @brief Checks if the schema was successfully parsed and loaded.
     *
     * @return true If schema is valid and ready, false otherwise.
     */
    [[nodiscard]] bool isSchemaLoaded() const noexcept;

    void setOutputChannel(Core::Memory::ObserverPtr<::Backend::IOutputChannel> outChannel) noexcept
    {
        m_outputChannel = outChannel;
    }

private:
    std::filesystem::path m_schemaPath;
    std::unique_ptr<XmlSchemaValidator> m_validator;
    Core::Memory::ObserverPtr<::Backend::IOutputChannel> m_outputChannel;
};

} // namespace Proxy::Handler
