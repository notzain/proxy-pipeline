#include "parsers/XercesOrderParser.hpp"

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES

#include "parsers/OrderSax2Handler.hpp"
#include <charconv>
#include <memory>
#include <string_view>
#include <utility>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/sax2/SAX2XMLReader.hpp>
#include <xercesc/sax2/XMLReaderFactory.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

namespace Proxy::Handler::Parsers {

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
        (void)guard;
    }

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

std::optional<Proxy::Model::Order> XercesOrderParser::parseDom(Common::Compat::span<const std::byte> xmlData)
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

std::optional<Proxy::Model::Order> XercesOrderParser::parseSax(Common::Compat::span<const std::byte> xmlData)
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

std::optional<Proxy::Model::Order> XercesOrderParser::parseReused(Common::Compat::span<const std::byte> xmlData)
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

} // namespace Proxy::Handler::Parsers

#endif // PROXY_HAS_XERCES

