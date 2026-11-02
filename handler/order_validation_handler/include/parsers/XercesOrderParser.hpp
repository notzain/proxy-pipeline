#pragma once

#include "Order.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <optional>

namespace Proxy::Handler::Parsers {

/**
 * @brief XML parser implementation utilizing Apache Xerces-C for Order model extraction.
 */
class XercesOrderParser {
public:
    /**
     * @brief Parses XML buffer to Order using Apache Xerces-C DOM API.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseDom(Common::Compat::span<const std::byte> xmlData);

    /**
     * @brief Parses XML buffer to Order using Apache Xerces-C SAX2 parser.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseSax(Common::Compat::span<const std::byte> xmlData);

    /**
     * @brief Parses XML buffer to Order reusing a thread-local Xerces DOM parser instance and document pool.
     */
    [[nodiscard]] static std::optional<Proxy::Model::Order> parseReused(Common::Compat::span<const std::byte> xmlData);
};

} // namespace Proxy::Handler::Parsers

