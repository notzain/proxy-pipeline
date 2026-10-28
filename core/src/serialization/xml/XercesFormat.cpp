#include "core/serialization/xml/XercesFormat.hpp"

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/framework/MemBufFormatTarget.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/util/XMLString.hpp>

namespace Core::Serialization::Xml {

namespace {

    struct XercesGlobalInitializer {
        XercesGlobalInitializer()
        {
            try {
                xercesc::XMLPlatformUtils::Initialize();
            } catch (...) {
            }
        }

        ~XercesGlobalInitializer()
        {
            try {
                xercesc::XMLPlatformUtils::Terminate();
            } catch (...) {
            }
        }
    };

    void ensureXercesInitialized()
    {
        static XercesGlobalInitializer initializer;
        (void)initializer;
    }

    class XercesString {
    public:
        explicit XercesString(const char* str)
            : m_unicode(xercesc::XMLString::transcode(str))
        {
        }

        explicit XercesString(std::string_view sv)
            : m_unicode(xercesc::XMLString::transcode(std::string(sv).c_str()))
        {
        }

        ~XercesString()
        {
            if (m_unicode) {
                xercesc::XMLString::release(&m_unicode);
            }
        }

        XercesString(const XercesString&) = delete;
        XercesString& operator=(const XercesString&) = delete;

        XercesString(XercesString&& other) noexcept
            : m_unicode(other.m_unicode)
        {
            other.m_unicode = nullptr;
        }

        XercesString& operator=(XercesString&& other) noexcept
        {
            if (this != &other) {
                if (m_unicode) {
                    xercesc::XMLString::release(&m_unicode);
                }
                m_unicode = other.m_unicode;
                other.m_unicode = nullptr;
            }
            return *this;
        }

        [[nodiscard]] const XMLCh* get() const noexcept
        {
            return m_unicode;
        }

    private:
        XMLCh* m_unicode { nullptr };
    };

    class TranscodeString {
    public:
        explicit TranscodeString(const XMLCh* unicode)
            : m_str(xercesc::XMLString::transcode(unicode))
        {
        }

        ~TranscodeString()
        {
            if (m_str) {
                xercesc::XMLString::release(&m_str);
            }
        }

        TranscodeString(const TranscodeString&) = delete;
        TranscodeString& operator=(const TranscodeString&) = delete;

        [[nodiscard]] const char* get() const noexcept
        {
            return m_str ? m_str : "";
        }

        [[nodiscard]] std::string_view view() const noexcept
        {
            return m_str ? std::string_view(m_str) : std::string_view();
        }

    private:
        char* m_str { nullptr };
    };

    void buildXmlNode(xercesc::DOMDocument* doc, const Ir::Value& val, xercesc::DOMElement* elem)
    {
        // Add attributes
        for (const auto& [attrName, attrValue] : val.attributes()) {
            XercesString xName(attrName.c_str());
            XercesString xVal(attrValue.c_str());
            elem->setAttribute(xName.get(), xVal.get());
        }

        if (val.isNull()) {
            return;
        }
        if (auto b = val.asBool()) {
            XercesString text(*b ? "true" : "false");
            elem->setTextContent(text.get());
            return;
        }
        if (val.isInt()) {
            std::string s = std::to_string(*val.asInt());
            XercesString text(s.c_str());
            elem->setTextContent(text.get());
            return;
        }
        if (val.isUInt()) {
            std::string s = std::to_string(*val.asUInt());
            XercesString text(s.c_str());
            elem->setTextContent(text.get());
            return;
        }
        if (val.isDouble()) {
            std::string s = std::to_string(*val.asDouble());
            XercesString text(s.c_str());
            elem->setTextContent(text.get());
            return;
        }
        if (auto s = val.asStringView()) {
            XercesString text(*s);
            elem->setTextContent(text.get());
            return;
        }
        if (auto* arr = val.asArray()) {
            XercesString itemTag("item");
            for (const auto& item : *arr) {
                xercesc::DOMElement* itemElem = doc->createElement(itemTag.get());
                elem->appendChild(itemElem);
                buildXmlNode(doc, item, itemElem);
            }
            return;
        }
        if (auto* obj = val.asObject()) {
            for (const auto& [fieldName, fieldValue] : *obj) {
                XercesString fieldTag(fieldName.c_str());
                xercesc::DOMElement* fieldElem = doc->createElement(fieldTag.get());
                elem->appendChild(fieldElem);
                buildXmlNode(doc, fieldValue, fieldElem);
            }
            return;
        }
    }

    Ir::Value parseXmlNode(xercesc::DOMElement* elem)
    {
        Ir::Value val;

        // Parse attributes
        if (elem->hasAttributes()) {
            xercesc::DOMNamedNodeMap* attrs = elem->getAttributes();
            const XMLSize_t attrCount = attrs->getLength();
            for (XMLSize_t i = 0; i < attrCount; ++i) {
                auto* attr = static_cast<xercesc::DOMAttr*>(attrs->item(i));
                TranscodeString nameStr(attr->getName());
                TranscodeString valStr(attr->getValue());
                val.setAttribute(std::string(nameStr.view()), std::string(valStr.view()));
            }
        }

        // Count element children
        std::size_t childCount = 0;
        bool allItems = true;
        for (xercesc::DOMNode* child = elem->getFirstChild(); child != nullptr; child = child->getNextSibling()) {
            if (child->getNodeType() == xercesc::DOMNode::ELEMENT_NODE) {
                ++childCount;
                TranscodeString nodeName(child->getNodeName());
                if (nodeName.view() != "item") {
                    allItems = false;
                }
            }
        }

        if (childCount == 0) {
            TranscodeString text(elem->getTextContent());
            std::string_view sv = text.view();
            auto start = sv.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) {
                val.raw() = Ir::ObjectType {};
                return val;
            }
            auto end = sv.find_last_not_of(" \t\r\n");
            std::string_view trimmed = sv.substr(start, end - start + 1);

            if (trimmed == "true") {
                val.raw() = true;
            } else if (trimmed == "false") {
                val.raw() = false;
            } else {
                char* endPtr = nullptr;
                long long ival = std::strtoll(trimmed.data(), &endPtr, 10);
                if (endPtr == trimmed.data() + trimmed.size()) {
                    val.raw() = static_cast<std::int64_t>(ival);
                } else {
                    double dval = std::strtod(trimmed.data(), &endPtr);
                    if (endPtr == trimmed.data() + trimmed.size()) {
                        val.raw() = dval;
                    } else {
                        val.raw() = std::string(trimmed);
                    }
                }
            }
            return val;
        }

        if (allItems && childCount > 0) {
            Ir::ArrayType arr;
            arr.reserve(childCount);
            for (xercesc::DOMNode* child = elem->getFirstChild(); child != nullptr; child = child->getNextSibling()) {
                if (child->getNodeType() == xercesc::DOMNode::ELEMENT_NODE) {
                    arr.push_back(parseXmlNode(static_cast<xercesc::DOMElement*>(child)));
                }
            }
            val.raw() = std::move(arr);
            return val;
        }

        Ir::ObjectType obj;
        for (xercesc::DOMNode* child = elem->getFirstChild(); child != nullptr; child = child->getNextSibling()) {
            if (child->getNodeType() == xercesc::DOMNode::ELEMENT_NODE) {
                TranscodeString nameStr(child->getNodeName());
                obj[std::string(nameStr.view())] = parseXmlNode(static_cast<xercesc::DOMElement*>(child));
            }
        }
        val.raw() = std::move(obj);
        return val;
    }

} // namespace

std::string XercesFormat::encode(const Ir::Value& ir, const Options& options)
{
    ensureXercesInitialized();

    XercesString lsStr("LS");
    xercesc::DOMImplementation* impl = xercesc::DOMImplementationRegistry::getDOMImplementation(lsStr.get());
    if (!impl) {
        return {};
    }

    XercesString rootTag(options.rootName.c_str());
    xercesc::DOMDocument* doc = impl->createDocument(nullptr, rootTag.get(), nullptr);
    if (!doc) {
        return {};
    }

    struct DocGuard {
        xercesc::DOMDocument* d;
        ~DocGuard()
        {
            if (d) {
                d->release();
            }
        }
    } docGuard { doc };

    xercesc::DOMElement* rootElem = doc->getDocumentElement();
    buildXmlNode(doc, ir, rootElem);

    xercesc::DOMLSSerializer* serializer = static_cast<xercesc::DOMImplementationLS*>(impl)->createLSSerializer();
    if (!serializer) {
        return {};
    }

    struct SerializerGuard {
        xercesc::DOMLSSerializer* s;
        ~SerializerGuard()
        {
            if (s) {
                s->release();
            }
        }
    } serGuard { serializer };

    if (serializer->getDomConfig()->canSetParameter(xercesc::XMLUni::fgDOMWRTFormatPrettyPrint, true)) {
        serializer->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMWRTFormatPrettyPrint, true);
    }
    if (serializer->getDomConfig()->canSetParameter(xercesc::XMLUni::fgDOMXMLDeclaration, false)) {
        serializer->getDomConfig()->setParameter(xercesc::XMLUni::fgDOMXMLDeclaration, false);
    }

    xercesc::MemBufFormatTarget target;
    xercesc::DOMLSOutput* output = static_cast<xercesc::DOMImplementationLS*>(impl)->createLSOutput();
    if (!output) {
        return {};
    }

    struct OutputGuard {
        xercesc::DOMLSOutput* o;
        ~OutputGuard()
        {
            if (o) {
                o->release();
            }
        }
    } outGuard { output };

    output->setByteStream(&target);
    serializer->write(doc, output);

    return std::string(reinterpret_cast<const char*>(target.getRawBuffer()), target.getLen());
}

std::optional<Ir::Value> XercesFormat::decode(std::string_view input, const Options& options)
{
    ensureXercesInitialized();

    if (input.empty()) {
        return std::nullopt;
    }

    try {
        xercesc::XercesDOMParser parser;
        parser.setValidationScheme(xercesc::XercesDOMParser::Val_Never);
        parser.setDoNamespaces(false);
        parser.setDoSchema(false);
        parser.setLoadExternalDTD(false);

        xercesc::MemBufInputSource memBuf(
            reinterpret_cast<const XMLByte*>(input.data()),
            input.size(),
            "XercesFormatBuffer",
            false);

        parser.parse(memBuf);

        xercesc::DOMDocument* doc = parser.getDocument();
        if (!doc) {
            return std::nullopt;
        }

        xercesc::DOMElement* root = doc->getDocumentElement();
        if (!root) {
            return std::nullopt;
        }

        if (!options.rootName.empty()) {
            TranscodeString rootNameStr(root->getNodeName());
            if (rootNameStr.view() != options.rootName) {
                return std::nullopt;
            }
        }

        return parseXmlNode(root);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Ir::Value> XercesFormat::decodeInPlace(void* data, std::size_t size, const Options& options)
{
    return decode(std::string_view(static_cast<const char*>(data), size), options);
}

} // namespace Core::Serialization::Xml

#endif // PROXY_HAS_XERCES
