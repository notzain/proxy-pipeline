#pragma once

#include "backend/message/MessageView.hpp"
#include <memory>

namespace Proxy::Handler {

/**
 * @brief Abstract interface for a message routing condition predicate.
 */
class IRouteCondition {
public:
    virtual ~IRouteCondition() = default;

    /**
     * @brief Evaluates whether the given message satisfies the condition.
     * @param message Message view to inspect.
     * @return True if condition matches, false otherwise.
     */
    [[nodiscard]] virtual bool matches(const Backend::MessageView& message) const noexcept = 0;

    /**
     * @brief Clones the condition instance.
     */
    [[nodiscard]] virtual std::unique_ptr<IRouteCondition> clone() const = 0;
};

} // namespace Proxy::Handler
