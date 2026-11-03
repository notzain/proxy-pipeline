#include "RouteRule.hpp"
#include "CompositeConditions.hpp"
#include "PortCondition.hpp"
#include "ProtocolCondition.hpp"
#include "RouteRuleBuilder.hpp"
#include "TextCondition.hpp"

namespace Proxy::Handler {

namespace {

    /**
     * @brief Converts legacy pattern to equivalent IRouteCondition.
     */
    struct LegacyPatternConverter {
        std::unique_ptr<IRouteCondition> operator()(const MatchCriteria::Protocol& c) const
        {
            return std::make_unique<ProtocolCondition>(c.expected);
        }

        std::unique_ptr<IRouteCondition> operator()(const MatchCriteria::PayloadContains& c) const
        {
            return std::make_unique<TextCondition>(c.pattern, TextMatchMode::Contains, 1);
        }

        std::unique_ptr<IRouteCondition> operator()(const MatchCriteria::PayloadPrefix& c) const
        {
            return std::make_unique<TextCondition>(c.prefix, TextMatchMode::Prefix, 1);
        }

        std::unique_ptr<IRouteCondition> operator()(const MatchCriteria::ClientPort& c) const
        {
            return std::make_unique<PortCondition>(c.port);
        }

        std::unique_ptr<IRouteCondition> operator()(const MatchCriteria::Fallback&) const
        {
            return std::make_unique<TextCondition>("", TextMatchMode::Contains, 0);
        }
    };

} // namespace

RouteRule::RouteRule(RoutePattern pattern, std::string targetChannelName)
    : m_condition(pattern.match(LegacyPatternConverter { }))
    , m_action(RouteAction::forward(std::move(targetChannelName)))
    , m_legacyPattern(std::make_unique<RoutePattern>(std::move(pattern)))
{
}

RouteRule::RouteRule(std::unique_ptr<IRouteCondition> condition, RouteAction action)
    : m_condition(std::move(condition))
    , m_action(std::move(action))
{
}

RouteRuleBuilder RouteRule::when(const IRouteCondition& condition)
{
    return RouteRuleBuilder(condition.clone());
}

bool RouteRule::matches(const Backend::MessageView& message) const noexcept
{
    return m_condition ? m_condition->matches(message) : false;
}

const std::string& RouteRule::targetChannelName() const noexcept
{
    static const std::string emptyString;
    if (const auto* fwd = m_action.asForward()) {
        return fwd->targetChannelName;
    }
    return emptyString;
}

Common::Memory::ObserverPtr<::Backend::IOutputChannel> RouteRule::targetChannel() const noexcept
{
    if (const auto* fwd = m_action.asForward()) {
        return fwd->targetChannel;
    }
    return nullptr;
}

void RouteRule::bindTargetChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept
{
    m_action.bindTargetChannel(channel);
}

const RouteAction& RouteRule::action() const noexcept
{
    return m_action;
}

RouteAction& RouteRule::action() noexcept
{
    return m_action;
}

const RoutePattern* RouteRule::legacyPattern() const noexcept
{
    return m_legacyPattern.get();
}

const IRouteCondition* RouteRule::condition() const noexcept
{
    return m_condition.get();
}

} // namespace Proxy::Handler
