#pragma once

#include "CompositeConditions.hpp"
#include "PortCondition.hpp"
#include "ProtocolCondition.hpp"
#include "RouteAction.hpp"
#include "RouteRule.hpp"
#include "TextCondition.hpp"
#include <memory>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Fluent factory for text-based conditions.
 */
class Text {
public:
    static TextCondition contains(std::string pattern)
    {
        return TextCondition(std::move(pattern), TextMatchMode::Contains, 1);
    }

    static TextCondition prefix(std::string pattern)
    {
        return TextCondition(std::move(pattern), TextMatchMode::Prefix, 1);
    }

    static TextCondition exact(std::string pattern)
    {
        return TextCondition(std::move(pattern), TextMatchMode::Exact, 1);
    }
};

/**
 * @brief Fluent factory for UDP client port conditions.
 */
class Port {
public:
    static PortCondition equals(std::uint16_t port)
    {
        return PortCondition(port);
    }
};

/**
 * @brief Fluent factory for ProtocolId conditions.
 */
class Protocol {
public:
    static ProtocolCondition equals(Backend::ProtocolId proto)
    {
        return ProtocolCondition(proto);
    }
};

/**
 * @brief Fluent builder for chaining actions onto a condition.
 */
class RouteRuleBuilder {
public:
    explicit RouteRuleBuilder(std::unique_ptr<IRouteCondition> condition)
        : m_condition(std::move(condition))
    {
    }

    /**
     * @brief Creates a rule that forwards matching messages to the destination channel.
     */
    [[nodiscard]] RouteRule forwardTo(std::string targetChannelName)
    {
        return RouteRule(std::move(m_condition), RouteAction::forward(std::move(targetChannelName)));
    }

    /**
     * @brief Creates a rule that explicitly rejects matching messages with a reason.
     */
    [[nodiscard]] RouteRule reject(std::string reason, int errorCode = 400)
    {
        return RouteRule(std::move(m_condition), RouteAction::reject(std::move(reason), errorCode));
    }

    /**
     * @brief Chains another condition with logical AND.
     */
    RouteRuleBuilder& and_(const IRouteCondition& other)
    {
        auto andCond = std::make_unique<AndCondition>();
        andCond->add(std::move(m_condition));
        andCond->add(other.clone());
        m_condition = std::move(andCond);
        return *this;
    }

    /**
     * @brief Chains another condition with logical OR.
     */
    RouteRuleBuilder& or_(const IRouteCondition& other)
    {
        auto orCond = std::make_unique<OrCondition>();
        orCond->add(std::move(m_condition));
        orCond->add(other.clone());
        m_condition = std::move(orCond);
        return *this;
    }

private:
    std::unique_ptr<IRouteCondition> m_condition;
};

} // namespace Proxy::Handler
