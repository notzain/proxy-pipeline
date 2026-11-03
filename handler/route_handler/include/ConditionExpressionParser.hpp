#pragma once

#include "IRouteCondition.hpp"
#include <memory>
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Parses human-readable route condition expressions into IRouteCondition trees.
 *
 * Supported syntax:
 *   - Leaf terms:
 *       contains("pattern" [, count])
 *       prefix("pattern")
 *       exact("pattern")
 *       port == 1234  or  port(1234)
 *       protocol == 1  or  protocol(1)
 *   - Operators:
 *       and, &&
 *       or, ||
 *       not, !
 *   - Parentheses:
 *       ( ... )
 *
 * Example:
 *   prefix("HDR:") and (port == 5000 or port == 8080) and not contains("SECRET")
 */
class ConditionExpressionParser {
public:
    /**
     * @brief Parses an expression string into a composite condition tree.
     * @param expr Expression text.
     * @return Unique pointer to the root condition tree, or nullptr on parse syntax error.
     */
    [[nodiscard]] static std::unique_ptr<IRouteCondition> parse(std::string_view expr);
};

} // namespace Proxy::Handler

