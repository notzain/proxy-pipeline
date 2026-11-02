#include "parsers/LibXml2OrderParser.hpp"

#if defined(PROXY_HAS_LIBXML2) && PROXY_HAS_LIBXML2

#include <charconv>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>
#include <string_view>
#include <utility>

namespace Proxy::Handler::Parsers {

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

} // namespace

std::optional<Proxy::Model::Order> LibXml2OrderParser::parseDom(Common::Compat::span<const std::byte> xmlData)
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

std::optional<Proxy::Model::Order> LibXml2OrderParser::parseReader(Common::Compat::span<const std::byte> xmlData)
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

std::optional<Proxy::Model::Order> LibXml2OrderParser::parseReused(Common::Compat::span<const std::byte> xmlData)
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

} // namespace Proxy::Handler::Parsers

#endif // PROXY_HAS_LIBXML2

