#include "XmlVerificationHandler.hpp"
#include "log/Log.hpp"

#include <libxml/parser.h>
#include <libxml/xmlschemas.h>

namespace {
extern "C" void xmlSilentErrorCallback(void*, const char*, ...)
{
}

extern "C" void xmlSilentWarningCallback(void*, const char*, ...)
{
}
} // namespace

namespace Proxy::Handler {

/**
 * @brief Internal PIMPL wrapping libxml2 schema context.
 */
class XmlSchemaValidator {
public:
    explicit XmlSchemaValidator(const std::filesystem::path& schemaPath)
    {
        xmlInitParser();

        xmlSchemaParserCtxtPtr parserCtxt = xmlSchemaNewParserCtxt(schemaPath.c_str());
        if (!parserCtxt) {
            Proxy::Log::error("[XmlVerificationHandler] Failed to create XSD parser context for '{}'", schemaPath.string());
            return;
        }

        m_schema = xmlSchemaParse(parserCtxt);
        xmlSchemaFreeParserCtxt(parserCtxt);

        if (!m_schema) {
            Proxy::Log::error("[XmlVerificationHandler] Failed to parse XSD schema from '{}'", schemaPath.string());
            return;
        }

        m_validCtxt = xmlSchemaNewValidCtxt(m_schema);
        if (!m_validCtxt) {
            Proxy::Log::error("[XmlVerificationHandler] Failed to create schema validation context for '{}'", schemaPath.string());
            return;
        }

        // Suppress default libxml2 stdout/stderr noise
        xmlSchemaSetValidErrors(m_validCtxt, xmlSilentErrorCallback, xmlSilentWarningCallback, nullptr);

        m_loaded = true;
        Proxy::Log::info("[XmlVerificationHandler] Loaded and compiled XSD schema: {}", schemaPath.string());
    }

    ~XmlSchemaValidator()
    {
        if (m_validCtxt) {
            xmlSchemaFreeValidCtxt(m_validCtxt);
            m_validCtxt = nullptr;
        }
        if (m_schema) {
            xmlSchemaFree(m_schema);
            m_schema = nullptr;
        }
        xmlCleanupParser();
    }

    [[nodiscard]] bool isLoaded() const noexcept
    {
        return m_loaded;
    }

    [[nodiscard]] bool validate(std::span<const std::byte> xmlData) const
    {
        if (!m_loaded || !m_schema) {
            return false;
        }

        if (xmlData.empty()) {
            return false;
        }

        xmlDocPtr doc = xmlReadMemory(
            reinterpret_cast<const char*>(xmlData.data()),
            static_cast<int>(xmlData.size()),
            "noname.xml",
            nullptr,
            XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);

        if (!doc) {
            return false;
        }

        std::lock_guard lock(m_mutex);
        int result = -1;
        if (m_validCtxt) {
            result = xmlSchemaValidateDoc(m_validCtxt, doc);
        }
        xmlFreeDoc(doc);

        return result == 0;
    }

private:
    xmlSchemaPtr m_schema { nullptr };
    xmlSchemaValidCtxtPtr m_validCtxt { nullptr };
    mutable std::mutex m_mutex;
    bool m_loaded { false };
};

XmlVerificationHandler::XmlVerificationHandler(std::filesystem::path schemaPath, std::string outputChannelName)
    : m_schemaPath(std::move(schemaPath))
    , m_outputChannelName(std::move(outputChannelName))
    , m_validator(std::make_unique<XmlSchemaValidator>(m_schemaPath))
{
}

XmlVerificationHandler::~XmlVerificationHandler() = default;

bool XmlVerificationHandler::isSchemaLoaded() const noexcept
{
    return m_validator && m_validator->isLoaded();
}

bool XmlVerificationHandler::validate(std::span<const std::byte> xmlData) const
{
    return m_validator && m_validator->validate(xmlData);
}

void XmlVerificationHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    if (validate(buffer)) {
        Proxy::Log::debug("[XmlVerificationHandler] Message from '{}' ({} bytes) verified against XSD schema. Forwarding to '{}'...",
            channelName, buffer.size(), m_outputChannelName);
        if (!send(m_outputChannelName, buffer)) {
            Proxy::Log::warn("[XmlVerificationHandler] Failed to forward validated message to '{}'", m_outputChannelName);
        }
    } else {
        Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes) REJECTED by XSD schema '{}'. Discarding message.",
            channelName, buffer.size(), m_schemaPath.string());
    }
}

} // namespace Proxy::Handler

