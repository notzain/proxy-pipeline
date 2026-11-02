#pragma once

#include "Order.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <optional>

namespace Proxy::Handler::Parsers {

/**
 * @brief Zero-copy and direct in-place DOM parsing for OrderView models.
 */
class PugiOrderParser {
public:
    /**
     * @brief Direct zero-copy DOM parser from pugixml nodes to OrderView, bypassing Intermediate Representation (IR).
     *
     * @param xmlData Mutable non-owning span of the XML buffer for in-place parsing.
     * @return std::optional<Proxy::Model::OrderView> Parsed order view, or std::nullopt on error.
     */
    [[nodiscard]] static std::optional<Proxy::Model::OrderView> parseDirect(Common::Compat::span<std::byte> xmlData);
};

} // namespace Proxy::Handler::Parsers

