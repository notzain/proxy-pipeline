#pragma once

#include "IRouteCondition.hpp"
#include "RouteAction.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/MessageView.hpp"
#include "backend/message/ProtocolId.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "common/types/Variant.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace Proxy::Handler {

class RouteRuleBuilder;

namespace MatchCriteria {

    /**
     * @brief Matches MessageView::protocol() against an expected ProtocolId.
     */
    struct Protocol {
        Backend::ProtocolId expected;
    };

    /**
     * @brief Matches substring inside MessageView::payload().
     */
    struct PayloadContains {
        std::string pattern;
    };

    /**
     * @brief Matches prefix of MessageView::payload().
     */
    struct PayloadPrefix {
        std::string prefix;
    };

    /**
     * @brief Matches originating client UDP port in UdpMetadata.
     */
    struct ClientPort {
        std::uint16_t port;
    };

    /**
     * @brief Unconditional match (catch-all / default fallback route).
     */
    struct Fallback { };

} // namespace MatchCriteria

/**
 * @brief Legacy pattern sum type retained for simple declarative rules.
 */
using RoutePattern = Common::Types::Variant<
    MatchCriteria::Protocol,
    MatchCriteria::PayloadContains,
    MatchCriteria::PayloadPrefix,
    MatchCriteria::ClientPort,
    MatchCriteria::Fallback>;

/**
 * @brief Declarative routing rule binding a condition to a routing action.
 */
class RouteRule {
public:
    /**
     * @brief Construct rule with legacy RoutePattern and target channel.
     */
    RouteRule(RoutePattern pattern, std::string targetChannelName);

    /**
     * @brief Construct rule with composable condition and RouteAction.
     */
    RouteRule(std::unique_ptr<IRouteCondition> condition, RouteAction action);

    /**
     * @brief Fluent builder entry point.
     */
    static RouteRuleBuilder when(const IRouteCondition& condition);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept;
    [[nodiscard]] const std::string& targetChannelName() const noexcept;
    [[nodiscard]] Common::Memory::ObserverPtr<::Backend::IOutputChannel> targetChannel() const noexcept;
    void bindTargetChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept;

    [[nodiscard]] const RouteAction& action() const noexcept;
    [[nodiscard]] RouteAction& action() noexcept;

    [[nodiscard]] const RoutePattern* legacyPattern() const noexcept;
    [[nodiscard]] const IRouteCondition* condition() const noexcept;

private:
    std::unique_ptr<IRouteCondition> m_condition;
    RouteAction m_action;
    std::unique_ptr<RoutePattern> m_legacyPattern;
};

} // namespace Proxy::Handler
