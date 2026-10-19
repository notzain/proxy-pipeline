#include "XmlSchemaValidator.hpp"
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

XmlSchemaValidator::XmlSchemaValidator(const std::filesystem::path& schemaPath)
{
    xmlInitParser();

    xmlSchemaParserCtxtPtr parserCtxt = xmlSchemaNewParserCtxt(schemaPath.c_str());
    if (!parserCtxt) {
        Proxy::Log::error("[XmlSchemaValidator] Failed to create XSD parser context for '{}'", schemaPath.string());
        return;
    }

    m_schema = xmlSchemaParse(parserCtxt);
    xmlSchemaFreeParserCtxt(parserCtxt);

    if (!m_schema) {
        Proxy::Log::error("[XmlSchemaValidator] Failed to parse XSD schema from '{}'", schemaPath.string());
        return;
    }

    m_loaded = true;
    Proxy::Log::info("[XmlSchemaValidator] Loaded and compiled XSD schema: {}", schemaPath.string());
}

XmlSchemaValidator::~XmlSchemaValidator()
{
    if (m_schema) {
        xmlSchemaFree(m_schema);
        m_schema = nullptr;
    }
    xmlCleanupParser();
}

XmlSchemaValidator::XmlSchemaValidator(XmlSchemaValidator&& other) noexcept
    : m_schema(other.m_schema)
    , m_loaded(other.m_loaded)
{
    other.m_schema = nullptr;
    other.m_loaded = false;
}

XmlSchemaValidator& XmlSchemaValidator::operator=(XmlSchemaValidator&& other) noexcept
{
    if (this != &other) {
        if (m_schema) {
            xmlSchemaFree(m_schema);
        }
        m_schema = other.m_schema;
        m_loaded = other.m_loaded;
        other.m_schema = nullptr;
        other.m_loaded = false;
    }
    return *this;
}

bool XmlSchemaValidator::isLoaded() const noexcept
{
    return m_loaded;
}

bool XmlSchemaValidator::validate(std::span<const std::byte> xmlData) const
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
    static thread_local struct ThreadLocalValidCtxt {
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

} // namespace Proxy::Handler

