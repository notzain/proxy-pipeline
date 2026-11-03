#include "CompositeConditions.hpp"

namespace Proxy::Handler {

// --- AndCondition ---
AndCondition::AndCondition(std::vector<std::unique_ptr<IRouteCondition>> conditions)
    : m_conditions(std::move(conditions))
{
}

void AndCondition::add(std::unique_ptr<IRouteCondition> condition)
{
    if (condition) {
        m_conditions.push_back(std::move(condition));
    }
}

bool AndCondition::matches(const Backend::MessageView& message) const noexcept
{
    for (const auto& cond : m_conditions) {
        if (!cond || !cond->matches(message)) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<IRouteCondition> AndCondition::clone() const
{
    std::vector<std::unique_ptr<IRouteCondition>> cloned;
    cloned.reserve(m_conditions.size());
    for (const auto& cond : m_conditions) {
        if (cond) {
            cloned.push_back(cond->clone());
        }
    }
    return std::make_unique<AndCondition>(std::move(cloned));
}

const std::vector<std::unique_ptr<IRouteCondition>>& AndCondition::conditions() const noexcept
{
    return m_conditions;
}

// --- OrCondition ---
OrCondition::OrCondition(std::vector<std::unique_ptr<IRouteCondition>> conditions)
    : m_conditions(std::move(conditions))
{
}

void OrCondition::add(std::unique_ptr<IRouteCondition> condition)
{
    if (condition) {
        m_conditions.push_back(std::move(condition));
    }
}

bool OrCondition::matches(const Backend::MessageView& message) const noexcept
{
    if (m_conditions.empty()) {
        return false;
    }
    for (const auto& cond : m_conditions) {
        if (cond && cond->matches(message)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<IRouteCondition> OrCondition::clone() const
{
    std::vector<std::unique_ptr<IRouteCondition>> cloned;
    cloned.reserve(m_conditions.size());
    for (const auto& cond : m_conditions) {
        if (cond) {
            cloned.push_back(cond->clone());
        }
    }
    return std::make_unique<OrCondition>(std::move(cloned));
}

const std::vector<std::unique_ptr<IRouteCondition>>& OrCondition::conditions() const noexcept
{
    return m_conditions;
}

// --- NotCondition ---
NotCondition::NotCondition(std::unique_ptr<IRouteCondition> condition)
    : m_condition(std::move(condition))
{
}

bool NotCondition::matches(const Backend::MessageView& message) const noexcept
{
    return m_condition ? !m_condition->matches(message) : true;
}

std::unique_ptr<IRouteCondition> NotCondition::clone() const
{
    return std::make_unique<NotCondition>(m_condition ? m_condition->clone() : nullptr);
}

const IRouteCondition* NotCondition::inner() const noexcept
{
    return m_condition.get();
}

} // namespace Proxy::Handler
