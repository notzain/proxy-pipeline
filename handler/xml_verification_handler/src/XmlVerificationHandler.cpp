#include "XmlVerificationHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
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

        m_loaded = true;
        Proxy::Log::info("[XmlVerificationHandler] Loaded and compiled XSD schema: {}", schemaPath.string());
    }

    ~XmlSchemaValidator()
    {
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

        // Thread-local validation context pool: eliminates all mutex contention across threads
        thread_local struct ThreadLocalValidCtxt {
            xmlSchemaPtr cachedSchema { nullptr };
            xmlSchemaValidCtxtPtr ctxt { nullptr };

            ~ThreadLocalValidCtxt()
            {
                if (ctxt) {
                    xmlSchemaFreeValidCtxt(ctxt);
                }
            }
        } tlCtxt;

        if (tlCtxt.cachedSchema != m_schema || !tlCtxt.ctxt) {
            if (tlCtxt.ctxt) {
                xmlSchemaFreeValidCtxt(tlCtxt.ctxt);
            }
            tlCtxt.ctxt = xmlSchemaNewValidCtxt(m_schema);
            tlCtxt.cachedSchema = m_schema;
            if (tlCtxt.ctxt) {
                xmlSchemaSetValidErrors(tlCtxt.ctxt, xmlSilentErrorCallback, xmlSilentWarningCallback, nullptr);
            }
        }

        int result = -1;
        if (tlCtxt.ctxt) {
            result = xmlSchemaValidateDoc(tlCtxt.ctxt, doc);
        }
        xmlFreeDoc(doc);

        return result == 0;
    }

private:
    xmlSchemaPtr m_schema { nullptr };
    bool m_loaded { false };
};

XmlVerificationHandler::XmlVerificationHandler() = default;

XmlVerificationHandler::XmlVerificationHandler(std::filesystem::path schemaPath, std::string outputChannelName)
    : m_schemaPath(std::move(schemaPath))
    , m_outputChannelName(std::move(outputChannelName))
    , m_validator(std::make_unique<XmlSchemaValidator>(m_schemaPath))
{
}

XmlVerificationHandler::~XmlVerificationHandler() = default;

bool XmlVerificationHandler::configure(const Core::Configuration& config, ::Backend::IBackend& backend)
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

void XmlVerificationHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    if (validate(buffer)) {
        Proxy::Log::debug("[XmlVerificationHandler] Message from '{}' ({} bytes) verified against XSD schema. Forwarding to '{}'...",
            channelName, buffer.size(), m_outputChannelName);
        if (m_outputChannel) {
            if (!m_outputChannel->send(buffer)) {
                Proxy::Log::warn("[XmlVerificationHandler] Failed to forward validated message to '{}'", m_outputChannelName);
            }
        } else if (!send(m_outputChannelName, buffer)) {
            Proxy::Log::warn("[XmlVerificationHandler] Failed to forward validated message to '{}'", m_outputChannelName);
        }
    } else {
        Proxy::Log::warn("[XmlVerificationHandler] Message from '{}' ({} bytes) REJECTED by XSD schema '{}'. Discarding message.",
            channelName, buffer.size(), m_schemaPath.string());
    }
}

} // namespace Proxy::Handler
