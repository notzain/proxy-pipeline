#pragma once

#include "core/IHandler.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

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
class XmlVerificationHandler : public Core::IHandler {
public:
    /**
     * @brief Constructs an XmlVerificationHandler with a schema file and output channel.
     *
     * @param schemaPath Path to the .xsd schema file.
     * @param outputChannelName Name of the output channel to forward valid messages to.
     */
    XmlVerificationHandler(std::filesystem::path schemaPath, std::string outputChannelName);

    ~XmlVerificationHandler() override;

    /**
     * @brief Ingress callback for incoming messages.
     * Validates buffer against schema and forwards to output channel if valid.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Byte buffer of the message payload.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

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

private:
    std::filesystem::path m_schemaPath;
    std::string m_outputChannelName;
    std::unique_ptr<XmlSchemaValidator> m_validator;
};

} // namespace Proxy::Handler
