#include "OrderValidationHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/serialization/xml/XmlSerializer.hpp"
#include "log/Log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>
#include <pugixml.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/sax2/Attributes.hpp>
#include <xercesc/sax2/DefaultHandler.hpp>
#include <xercesc/sax2/SAX2XMLReader.hpp>
#include <xercesc/sax2/XMLReaderFactory.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

namespace Proxy::Handler {

OrderValidationHandler::OrderValidationHandler(std::string outputChannelName)
    : m_outputChannelName(std::move(outputChannelName))
{
}

OrderValidationHandler::~OrderValidationHandler() = default;

bool OrderValidationHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    m_inputChannelName = config.getOr<std::string>("order.input_channel", config.getOr<std::string>("channel.input", "order_in"));
    m_outputChannelName = config.getOr<std::string>("order.output_channel", config.getOr<std::string>("channel.output", "order_out"));

    auto inChannel = backend.createInputChannel(m_inputChannelName, config);
    if (!inChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create input channel '{}'", m_inputChannelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_inputChannelName, std::move(inChannel));

    auto outChannel = backend.createOutputChannel(m_outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create output channel '{}'", m_outputChannelName);
        return false;
    }
    m_cachedOutputChannel = registerOutputChannel(m_outputChannelName, std::move(outChannel));

    return true;
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrder(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::Order>::deserialize(xmlStr, { .rootName = "order" });
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderView(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    std::string_view xmlStr(reinterpret_cast<const char*>(xmlData.data()), xmlData.size());
    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::OrderView>::deserialize(xmlStr, { .rootName = "order" });
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderViewInPlace(std::span<std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    return Core::Serialization::Xml::XmlDeserializer<Proxy::Model::OrderView>::deserializeInPlace(
        xmlData.data(),
        xmlData.size(),
        { .rootName = "order" });
}

std::optional<Proxy::Model::OrderView> OrderValidationHandler::parseOrderDirect(std::span<std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer_inplace(xmlData.data(), xmlData.size(), pugi::parse_default);
    if (!result) {
        return std::nullopt;
    }

    pugi::xml_node root = doc.child("order");
    if (!root) {
        root = doc.first_child();
    }
    if (!root) {
        return std::nullopt;
    }

    auto customerNode = root.child("customer");
    auto itemNode = root.child("item");
    auto quantityNode = root.child("quantity");

    if (!customerNode || !itemNode || !quantityNode) {
        return std::nullopt;
    }

    std::string_view customerStr = customerNode.text().as_string();
    std::string_view itemStr = itemNode.text().as_string();
    std::string_view qtyStr = quantityNode.text().as_string();

    std::uint32_t qty = 0;
    auto [ptr, ec] = std::from_chars(qtyStr.data(), qtyStr.data() + qtyStr.size(), qty);
    if (ec != std::errc()) {
        return std::nullopt;
    }

    return Proxy::Model::OrderView {
        .customer = customerStr,
        .item = itemStr,
        .quantity = qty
    };
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    xmlDocPtr doc = xmlReadMemory(
        reinterpret_cast<const char*>(xmlData.data()),
        static_cast<int>(xmlData.size()),
        "noname.xml",
        nullptr,
        XML_PARSE_NOBLANKS | XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);

    if (!doc) {
        return std::nullopt;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, reinterpret_cast<const xmlChar*>("order")) != 0) {
        xmlFreeDoc(doc);
        return std::nullopt;
    }

    std::string customer;
    std::string item;
    std::uint32_t quantity = 0;
    bool hasCustomer = false;
    bool hasItem = false;
    bool hasQuantity = false;

    for (xmlNodePtr cur = root->children; cur != nullptr; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && cur->children && cur->children->type == XML_TEXT_NODE && cur->children->content) {
            std::string_view val(reinterpret_cast<const char*>(cur->children->content));
            if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("customer")) == 0) {
                customer = val;
                hasCustomer = true;
            } else if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("item")) == 0) {
                item = val;
                hasItem = true;
            } else if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("quantity")) == 0) {
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), quantity);
                if (ec == std::errc()) {
                    hasQuantity = true;
                }
            }
        }
    }

    xmlFreeDoc(doc);

    if (!hasCustomer || !hasItem || !hasQuantity) {
        return std::nullopt;
    }

    return Proxy::Model::Order {
        .customer = std::move(customer),
        .item = std::move(item),
        .quantity = quantity
    };
}

namespace {
    struct XercesInitGuard {
        XercesInitGuard()
        {
            try {
                xercesc::XMLPlatformUtils::Initialize();
            } catch (...) {
            }
        }
        ~XercesInitGuard()
        {
            try {
                xercesc::XMLPlatformUtils::Terminate();
            } catch (...) {
            }
        }
    };

    void ensureXercesInitialized()
    {
        static XercesInitGuard guard;
    }
} // namespace

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXerces(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    ensureXercesInitialized();

    try {
        xercesc::MemBufInputSource memBuf(
            reinterpret_cast<const XMLByte*>(xmlData.data()),
            static_cast<const XMLSize_t>(xmlData.size()),
            "order_buf",
            false);

        xercesc::XercesDOMParser parser;
        parser.setDoNamespaces(false);
        parser.setDoSchema(false);
        parser.setValidationScheme(xercesc::XercesDOMParser::Val_Never);

        parser.parse(memBuf);

        xercesc::DOMDocument* doc = parser.getDocument();
        if (!doc) {
            return std::nullopt;
        }

        xercesc::DOMElement* root = doc->getDocumentElement();
        if (!root) {
            return std::nullopt;
        }

        static const XMLCh tagOrder[] = { 'o', 'r', 'd', 'e', 'r', 0 };
        static const XMLCh tagCustomer[] = { 'c', 'u', 's', 't', 'o', 'm', 'e', 'r', 0 };
        static const XMLCh tagItem[] = { 'i', 't', 'e', 'm', 0 };
        static const XMLCh tagQuantity[] = { 'q', 'u', 'a', 'n', 't', 'i', 't', 'y', 0 };

        if (!xercesc::XMLString::equals(root->getTagName(), tagOrder)) {
            return std::nullopt;
        }

        std::string customer;
        std::string item;
        std::uint32_t quantity = 0;
        bool hasCustomer = false;
        bool hasItem = false;
        bool hasQuantity = false;

        for (xercesc::DOMNode* child = root->getFirstChild(); child != nullptr; child = child->getNextSibling()) {
            if (child->getNodeType() == xercesc::DOMNode::ELEMENT_NODE) {
                const XMLCh* nodeName = child->getNodeName();
                if (!nodeName) {
                    continue;
                }

                if (xercesc::XMLString::equals(nodeName, tagCustomer)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        customer = textVal;
                        hasCustomer = true;
                        xercesc::XMLString::release(&textVal);
                    }
                } else if (xercesc::XMLString::equals(nodeName, tagItem)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        item = textVal;
                        hasItem = true;
                        xercesc::XMLString::release(&textVal);
                    }
                } else if (xercesc::XMLString::equals(nodeName, tagQuantity)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        std::string_view val(textVal);
                        auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), quantity);
                        if (ec == std::errc()) {
                            hasQuantity = true;
                        }
                        xercesc::XMLString::release(&textVal);
                    }
                }
            }
        }

        if (!hasCustomer || !hasItem || !hasQuantity) {
            return std::nullopt;
        }

        return Proxy::Model::Order {
            .customer = std::move(customer),
            .item = std::move(item),
            .quantity = quantity
        };
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2Reader(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    xmlTextReaderPtr reader = xmlReaderForMemory(
        reinterpret_cast<const char*>(xmlData.data()),
        static_cast<int>(xmlData.size()),
        "order.xml",
        nullptr,
        XML_PARSE_NOBLANKS | XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);

    if (!reader) {
        return std::nullopt;
    }

    std::string customer;
    std::string item;
    std::uint32_t quantity = 0;
    bool hasCustomer = false;
    bool hasItem = false;
    bool hasQuantity = false;
    bool insideOrder = false;

    int ret = xmlTextReaderRead(reader);
    while (ret == 1) {
        int nodeType = xmlTextReaderNodeType(reader);
        if (nodeType == XML_READER_TYPE_ELEMENT) {
            const xmlChar* name = xmlTextReaderConstName(reader);
            if (!insideOrder) {
                if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("order")) == 0) {
                    insideOrder = true;
                } else {
                    xmlFreeTextReader(reader);
                    return std::nullopt;
                }
            } else {
                if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("customer")) == 0) {
                    xmlChar* val = xmlTextReaderReadString(reader);
                    if (val) {
                        customer = reinterpret_cast<const char*>(val);
                        hasCustomer = true;
                        xmlFree(val);
                    }
                } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("item")) == 0) {
                    xmlChar* val = xmlTextReaderReadString(reader);
                    if (val) {
                        item = reinterpret_cast<const char*>(val);
                        hasItem = true;
                        xmlFree(val);
                    }
                } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("quantity")) == 0) {
                    xmlChar* val = xmlTextReaderReadString(reader);
                    if (val) {
                        std::string_view sv(reinterpret_cast<const char*>(val));
                        auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), quantity);
                        if (ec == std::errc()) {
                            hasQuantity = true;
                        }
                        xmlFree(val);
                    }
                }
            }
        }
        ret = xmlTextReaderRead(reader);
    }

    xmlFreeTextReader(reader);

    if (ret < 0 || !hasCustomer || !hasItem || !hasQuantity) {
        return std::nullopt;
    }

    return Proxy::Model::Order {
        .customer = std::move(customer),
        .item = std::move(item),
        .quantity = quantity
    };
}

namespace {
    class OrderSax2Handler : public xercesc::DefaultHandler {
    public:
        enum class TargetField {
            None,
            Customer,
            Item,
            Quantity
        };

        void startElement(
            const XMLCh* const /*uri*/,
            const XMLCh* const localname,
            const XMLCh* const qname,
            const xercesc::Attributes& /*attrs*/) override
        {
            const XMLCh* name = (localname && *localname) ? localname : qname;
            if (!name) {
                return;
            }

            static const XMLCh tagOrder[] = { 'o', 'r', 'd', 'e', 'r', 0 };
            static const XMLCh tagCustomer[] = { 'c', 'u', 's', 't', 'o', 'm', 'e', 'r', 0 };
            static const XMLCh tagItem[] = { 'i', 't', 'e', 'm', 0 };
            static const XMLCh tagQuantity[] = { 'q', 'u', 'a', 'n', 't', 'i', 't', 'y', 0 };

            if (!m_insideOrder) {
                if (xercesc::XMLString::equals(name, tagOrder)) {
                    m_insideOrder = true;
                }
            } else {
                if (xercesc::XMLString::equals(name, tagCustomer)) {
                    m_currentField = TargetField::Customer;
                    m_currentBuffer.clear();
                } else if (xercesc::XMLString::equals(name, tagItem)) {
                    m_currentField = TargetField::Item;
                    m_currentBuffer.clear();
                } else if (xercesc::XMLString::equals(name, tagQuantity)) {
                    m_currentField = TargetField::Quantity;
                    m_currentBuffer.clear();
                } else {
                    m_currentField = TargetField::None;
                }
            }
        }

        void characters(const XMLCh* const chars, const XMLSize_t length) override
        {
            if (m_currentField != TargetField::None && chars != nullptr && length > 0) {
                char* transcoded = xercesc::XMLString::transcode(chars);
                if (transcoded) {
                    // transcode() returns up to null-terminator for the whole buffer or slice
                    m_currentBuffer.append(transcoded, std::min<std::size_t>(std::strlen(transcoded), length));
                    xercesc::XMLString::release(&transcoded);
                }
            }
        }

        void endElement(
            const XMLCh* const /*uri*/,
            const XMLCh* const localname,
            const XMLCh* const qname) override
        {
            const XMLCh* name = (localname && *localname) ? localname : qname;
            if (!name) {
                return;
            }

            static const XMLCh tagCustomer[] = { 'c', 'u', 's', 't', 'o', 'm', 'e', 'r', 0 };
            static const XMLCh tagItem[] = { 'i', 't', 'e', 'm', 0 };
            static const XMLCh tagQuantity[] = { 'q', 'u', 'a', 'n', 't', 'i', 't', 'y', 0 };

            if (m_currentField == TargetField::Customer && xercesc::XMLString::equals(name, tagCustomer)) {
                m_customer = std::move(m_currentBuffer);
                m_hasCustomer = true;
                m_currentField = TargetField::None;
            } else if (m_currentField == TargetField::Item && xercesc::XMLString::equals(name, tagItem)) {
                m_item = std::move(m_currentBuffer);
                m_hasItem = true;
                m_currentField = TargetField::None;
            } else if (m_currentField == TargetField::Quantity && xercesc::XMLString::equals(name, tagQuantity)) {
                auto [ptr, ec] = std::from_chars(m_currentBuffer.data(), m_currentBuffer.data() + m_currentBuffer.size(), m_quantity);
                if (ec == std::errc()) {
                    m_hasQuantity = true;
                }
                m_currentField = TargetField::None;
            }
        }

        [[nodiscard]] bool isValid() const noexcept
        {
            return m_insideOrder && m_hasCustomer && m_hasItem && m_hasQuantity;
        }

        [[nodiscard]] Proxy::Model::Order toOrder() &&
        {
            return Proxy::Model::Order {
                .customer = std::move(m_customer),
                .item = std::move(m_item),
                .quantity = m_quantity
            };
        }

    private:
        TargetField m_currentField { TargetField::None };
        std::string m_currentBuffer;
        bool m_insideOrder { false };
        std::string m_customer;
        std::string m_item;
        std::uint32_t m_quantity { 0 };
        bool m_hasCustomer { false };
        bool m_hasItem { false };
        bool m_hasQuantity { false };
    };
} // namespace

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXercesSax(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    ensureXercesInitialized();

    try {
        xercesc::MemBufInputSource memBuf(
            reinterpret_cast<const XMLByte*>(xmlData.data()),
            static_cast<const XMLSize_t>(xmlData.size()),
            "order_sax_buf",
            false);

        std::unique_ptr<xercesc::SAX2XMLReader> reader(xercesc::XMLReaderFactory::createXMLReader());
        if (!reader) {
            return std::nullopt;
        }

        reader->setFeature(xercesc::XMLUni::fgSAX2CoreNameSpaces, false);
        reader->setFeature(xercesc::XMLUni::fgSAX2CoreValidation, false);
        reader->setFeature(xercesc::XMLUni::fgXercesDynamic, false);
        reader->setFeature(xercesc::XMLUni::fgXercesSchema, false);

        OrderSax2Handler handler;
        reader->setContentHandler(&handler);
        reader->setErrorHandler(&handler);

        reader->parse(memBuf);

        if (!handler.isValid()) {
            return std::nullopt;
        }

        return std::move(handler).toOrder();
    } catch (...) {
        return std::nullopt;
    }
}

namespace {
    struct ThreadLocalLibXml2Ctxt {
        xmlParserCtxtPtr ctxt { nullptr };

        ThreadLocalLibXml2Ctxt()
        {
            ctxt = xmlNewParserCtxt();
        }

        ~ThreadLocalLibXml2Ctxt()
        {
            if (ctxt) {
                xmlFreeParserCtxt(ctxt);
            }
        }
    };

    struct ThreadLocalXercesParser {
        std::unique_ptr<xercesc::XercesDOMParser> parser;

        ThreadLocalXercesParser()
        {
            ensureXercesInitialized();
            parser = std::make_unique<xercesc::XercesDOMParser>();
            parser->setDoNamespaces(false);
            parser->setDoSchema(false);
            parser->setValidationScheme(xercesc::XercesDOMParser::Val_Never);
        }
    };
} // namespace

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderLibXml2Reused(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    thread_local ThreadLocalLibXml2Ctxt tlCtxt;
    if (!tlCtxt.ctxt) {
        return std::nullopt;
    }

    xmlDocPtr doc = xmlCtxtReadMemory(
        tlCtxt.ctxt,
        reinterpret_cast<const char*>(xmlData.data()),
        static_cast<int>(xmlData.size()),
        "order.xml",
        nullptr,
        XML_PARSE_NOBLANKS | XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR);

    if (!doc) {
        return std::nullopt;
    }

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root || xmlStrcmp(root->name, reinterpret_cast<const xmlChar*>("order")) != 0) {
        xmlFreeDoc(doc);
        return std::nullopt;
    }

    std::string customer;
    std::string item;
    std::uint32_t quantity = 0;
    bool hasCustomer = false;
    bool hasItem = false;
    bool hasQuantity = false;

    for (xmlNodePtr cur = root->children; cur != nullptr; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE && cur->children && cur->children->type == XML_TEXT_NODE && cur->children->content) {
            std::string_view val(reinterpret_cast<const char*>(cur->children->content));
            if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("customer")) == 0) {
                customer = val;
                hasCustomer = true;
            } else if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("item")) == 0) {
                item = val;
                hasItem = true;
            } else if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("quantity")) == 0) {
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), quantity);
                if (ec == std::errc()) {
                    hasQuantity = true;
                }
            }
        }
    }

    xmlFreeDoc(doc);

    if (!hasCustomer || !hasItem || !hasQuantity) {
        return std::nullopt;
    }

    return Proxy::Model::Order {
        .customer = std::move(customer),
        .item = std::move(item),
        .quantity = quantity
    };
}

std::optional<Proxy::Model::Order> OrderValidationHandler::parseOrderXercesReused(std::span<const std::byte> xmlData) const
{
    if (xmlData.empty()) {
        return std::nullopt;
    }

    thread_local ThreadLocalXercesParser tlParser;
    if (!tlParser.parser) {
        return std::nullopt;
    }

    try {
        xercesc::MemBufInputSource memBuf(
            reinterpret_cast<const XMLByte*>(xmlData.data()),
            static_cast<const XMLSize_t>(xmlData.size()),
            "order_reused_buf",
            false);

        tlParser.parser->resetDocumentPool();
        tlParser.parser->parse(memBuf);

        xercesc::DOMDocument* doc = tlParser.parser->getDocument();
        if (!doc) {
            return std::nullopt;
        }

        xercesc::DOMElement* root = doc->getDocumentElement();
        if (!root) {
            return std::nullopt;
        }

        static const XMLCh tagOrder[] = { 'o', 'r', 'd', 'e', 'r', 0 };
        static const XMLCh tagCustomer[] = { 'c', 'u', 's', 't', 'o', 'm', 'e', 'r', 0 };
        static const XMLCh tagItem[] = { 'i', 't', 'e', 'm', 0 };
        static const XMLCh tagQuantity[] = { 'q', 'u', 'a', 'n', 't', 'i', 't', 'y', 0 };

        if (!xercesc::XMLString::equals(root->getTagName(), tagOrder)) {
            return std::nullopt;
        }

        std::string customer;
        std::string item;
        std::uint32_t quantity = 0;
        bool hasCustomer = false;
        bool hasItem = false;
        bool hasQuantity = false;

        for (xercesc::DOMNode* child = root->getFirstChild(); child != nullptr; child = child->getNextSibling()) {
            if (child->getNodeType() == xercesc::DOMNode::ELEMENT_NODE) {
                const XMLCh* nodeName = child->getNodeName();
                if (!nodeName) {
                    continue;
                }

                if (xercesc::XMLString::equals(nodeName, tagCustomer)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        customer = textVal;
                        hasCustomer = true;
                        xercesc::XMLString::release(&textVal);
                    }
                } else if (xercesc::XMLString::equals(nodeName, tagItem)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        item = textVal;
                        hasItem = true;
                        xercesc::XMLString::release(&textVal);
                    }
                } else if (xercesc::XMLString::equals(nodeName, tagQuantity)) {
                    char* textVal = xercesc::XMLString::transcode(child->getTextContent());
                    if (textVal) {
                        std::string_view val(textVal);
                        auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), quantity);
                        if (ec == std::errc()) {
                            hasQuantity = true;
                        }
                        xercesc::XMLString::release(&textVal);
                    }
                }
            }
        }

        if (!hasCustomer || !hasItem || !hasQuantity) {
            return std::nullopt;
        }

        return Proxy::Model::Order {
            .customer = std::move(customer),
            .item = std::move(item),
            .quantity = quantity
        };
    } catch (...) {
        return std::nullopt;
    }
}

bool OrderValidationHandler::validateOrder(const Proxy::Model::Order& order) const
{
    // Customer validation:
    // Must be between 2 and 50 characters, only letters, spaces, or hyphens
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        Proxy::Log::debug("[OrderValidationHandler] Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    // Must be between 2 and 50 characters, alphanumeric or spaces/hyphens/underscores
    if (order.item.size() < 2 || order.item.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        Proxy::Log::debug("[OrderValidationHandler] Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    // Must be positive and within reasonable batch limit (<= 10,000)
    if (order.quantity == 0 || order.quantity > 10000) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

bool OrderValidationHandler::validateOrderView(const Proxy::Model::OrderView& order) const
{
    // Customer validation:
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid customer length: '{}'", order.customer);
        return false;
    }

    bool validCustomerChars = std::all_of(order.customer.begin(), order.customer.end(), [](unsigned char c) {
        return std::isalpha(c) || std::isspace(c) || c == '-';
    });
    if (!validCustomerChars) {
        Proxy::Log::debug("[OrderValidationHandler] Customer contains invalid characters: '{}'", order.customer);
        return false;
    }

    // Item validation:
    if (order.item.size() < 2 || order.item.size() > 50) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid item length: '{}'", order.item);
        return false;
    }

    bool validItemChars = std::all_of(order.item.begin(), order.item.end(), [](unsigned char c) {
        return std::isalnum(c) || std::isspace(c) || c == '-' || c == '_';
    });
    if (!validItemChars) {
        Proxy::Log::debug("[OrderValidationHandler] Item contains invalid characters: '{}'", order.item);
        return false;
    }

    // Quantity validation:
    if (order.quantity == 0 || order.quantity > 10000) {
        Proxy::Log::debug("[OrderValidationHandler] Invalid quantity: {}", order.quantity);
        return false;
    }

    return true;
}

namespace {

    constexpr auto makeCustomerCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        return table;
    }

    constexpr auto makeItemCharTable()
    {
        std::array<bool, 256> table { };
        for (unsigned char c = 'a'; c <= 'z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = 'A'; c <= 'Z'; ++c) {
            table[c] = true;
        }
        for (unsigned char c = '0'; c <= '9'; ++c) {
            table[c] = true;
        }
        table[' '] = true;
        table['-'] = true;
        table['_'] = true;
        return table;
    }

    constexpr auto CustomerCharTable = makeCustomerCharTable();
    constexpr auto ItemCharTable = makeItemCharTable();

} // namespace

bool OrderValidationHandler::validateOrderFast(const Proxy::Model::OrderView& order) const
{
    // Customer validation: length 2..50
    if (order.customer.size() < 2 || order.customer.size() > 50) {
        return false;
    }

    for (char ch : order.customer) {
        if (!CustomerCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Item validation: length 2..50
    if (order.item.size() < 2 || order.item.size() > 50) {
        return false;
    }

    for (char ch : order.item) {
        if (!ItemCharTable[static_cast<unsigned char>(ch)]) {
            return false;
        }
    }

    // Quantity validation: 1..10000
    if (order.quantity == 0 || order.quantity > 10000) {
        return false;
    }

    return true;
}

void OrderValidationHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    auto order = parseOrder(buffer);
    if (!order.has_value()) {
        Proxy::Log::warn("[OrderValidationHandler] Message from '{}' ({} bytes) failed XML deserialization to Order. Discarding.",
            channelName, buffer.size());
        return;
    }

    if (!validateOrder(*order)) {
        Proxy::Log::warn("[OrderValidationHandler] Order for customer '{}' / item '{}' / qty {} failed domain validation. Discarding.",
            order->customer, order->item, order->quantity);
        return;
    }

    Proxy::Log::debug("[OrderValidationHandler] Order validated for customer '{}', item '{}', qty {}. Forwarding to '{}'...",
        order->customer, order->item, order->quantity, m_outputChannelName);

    if (!send(m_outputChannelName, buffer)) {
        Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", m_outputChannelName);
    }
}

void OrderValidationHandler::onMessageReceivedFast(std::string_view channelName, std::span<std::byte> buffer)
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

    if (!m_cachedOutputChannel) {
        std::shared_lock lock(m_outputMutex);
        auto it = m_outputChannels.find(m_outputChannelName);
        if (it != m_outputChannels.end()) {
            m_cachedOutputChannel = it->second.get();
        }
    }

    if (m_cachedOutputChannel) {
        if (!m_cachedOutputChannel->send(buffer)) {
            Proxy::Log::warn("[OrderValidationHandler] Failed to forward validated order to '{}'", m_outputChannelName);
        }
    } else {
        Proxy::Log::warn("[OrderValidationHandler] Output channel '{}' not found", m_outputChannelName);
    }
}

} // namespace Proxy::Handler
