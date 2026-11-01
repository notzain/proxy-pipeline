#pragma once

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES

#include "Order.hpp"
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <xercesc/sax2/Attributes.hpp>
#include <xercesc/sax2/DefaultHandler.hpp>
#include <xercesc/util/XMLString.hpp>

namespace Proxy::Handler::Parsers {

/**
 * @brief SAX2 content handler specifically extracting Order fields from Xerces SAX events.
 */
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

} // namespace Proxy::Handler::Parsers

#endif // PROXY_HAS_XERCES

