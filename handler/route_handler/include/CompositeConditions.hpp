#pragma once

#include "IRouteCondition.hpp"
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Logical AND composition of conditions.
 */
class AndCondition : public IRouteCondition {
public:
    AndCondition() = default;
    explicit AndCondition(std::vector<std::unique_ptr<IRouteCondition>> conditions);

    void add(std::unique_ptr<IRouteCondition> condition);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const std::vector<std::unique_ptr<IRouteCondition>>& conditions() const noexcept;

private:
    std::vector<std::unique_ptr<IRouteCondition>> m_conditions;
};

/**
 * @brief Logical OR composition of conditions.
 */
class OrCondition : public IRouteCondition {
public:
    OrCondition() = default;
    explicit OrCondition(std::vector<std::unique_ptr<IRouteCondition>> conditions);

    void add(std::unique_ptr<IRouteCondition> condition);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const std::vector<std::unique_ptr<IRouteCondition>>& conditions() const noexcept;

private:
    std::vector<std::unique_ptr<IRouteCondition>> m_conditions;
};

/**
 * @brief Logical NOT negation of a condition.
 */
class NotCondition : public IRouteCondition {
public:
    explicit NotCondition(std::unique_ptr<IRouteCondition> condition);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const IRouteCondition* inner() const noexcept;

private:
    std::unique_ptr<IRouteCondition> m_condition;
};

} // namespace Proxy::Handler
