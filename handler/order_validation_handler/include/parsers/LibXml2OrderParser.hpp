#pragma once

#include "Order.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <optional>

namespace Proxy::Handler::Parsers {

/**
 * @brief XML parser implementation utilizing libxml2 for Order model extraction.
 */
class LibXml2OrderParser {
public:
    /**
     * @brief Parses XML buffer to Order using libxml2 DOM API.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseDom(Common::Compat::span<const std::byte> xmlData);

    /**
     * @brief Parses XML buffer to Order using libxml2 xmlReader streaming cursor API.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseReader(Common::Compat::span<const std::byte> xmlData);

    /**
     * @brief Parses XML buffer to Order reusing a thread-local libxml2 parser context.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseReused(Common::Compat::span<const std::byte> xmlData);
};

} // namespace Proxy::Handler::Parsers

