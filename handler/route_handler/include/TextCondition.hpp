#pragma once

#include "IRouteCondition.hpp"
#include <cstddef>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Mode of text condition inspection.
 */
enum class TextMatchMode {
    Contains,
    Prefix,
    Exact
};

/**
 * @brief Condition inspecting payload text content.
 */
class TextCondition : public IRouteCondition {
public:
    TextCondition(std::string pattern, TextMatchMode mode = TextMatchMode::Contains, std::size_t minTimes = 1);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const std::string& pattern() const noexcept;
    [[nodiscard]] TextMatchMode mode() const noexcept;
    [[nodiscard]] std::size_t minTimes() const noexcept;

    /**
     * @brief Fluent builder chaining for minimum occurrence count.
     */
    TextCondition& times(std::size_t count) noexcept;

private:
    std::string m_pattern;
    TextMatchMode m_mode;
    std::size_t m_minTimes { 1 };
};

} // namespace Proxy::Handler
