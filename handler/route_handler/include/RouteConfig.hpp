#pragma once

#include "CompositeConditions.hpp"
#include "ConditionExpressionParser.hpp"
#include "PortCondition.hpp"
#include "ProtocolCondition.hpp"
#include "RouteAction.hpp"
#include "RouteRule.hpp"
#include "TextCondition.hpp"
#include "core/config/ConfigItem.hpp"
#include "core/config/ConfigSection.hpp"
#include "core/config/Configuration.hpp"
#include <fmt/format.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Structured configuration for an individual route rule.
 */
struct RouteRuleConfig {
    std::string name;
    std::string type { "contains" };
    std::string pattern;
    std::string target;
    std::string actionType { "forward" };
    std::string rejectReason;
    std::string expr;
    int rejectErrorCode { 400 };
    int numericValue { 0 };
    int times { 1 };

    static constexpr std::string_view KeySuffixType { "type" };
    static constexpr std::string_view KeySuffixPattern { "pattern" };
    static constexpr std::string_view KeySuffixTarget { "target" };
    static constexpr std::string_view KeySuffixValue { "value" };
    static constexpr std::string_view KeySuffixTimes { "times" };
    static constexpr std::string_view KeySuffixAction { "action" };
    static constexpr std::string_view KeySuffixRejectReason { "reason" };
    static constexpr std::string_view KeySuffixRejectErrorCode { "error_code" };
    static constexpr std::string_view KeySuffixExpr { "expr" };

    /**
     * @brief Constructs a RouteRuleConfig from the given prefix and configuration store.
     */
    static RouteRuleConfig fromPrefix(std::string_view ruleName,
        std::string_view prefix,
        const Core::Config::Configuration& config)
    {
        RouteRuleConfig rule;
        rule.name = std::string(ruleName);
        rule.expr = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixExpr), "");
        rule.type = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixType), "contains");
        rule.pattern = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixPattern), "");
        rule.target = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixTarget), "");
        rule.actionType = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixAction), "forward");
        rule.rejectReason = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixRejectReason), "Message rejected by route rule");
        rule.rejectErrorCode = config.getOr<int>(fmt::format("{}{}", prefix, KeySuffixRejectErrorCode), 400);
        rule.numericValue = config.getOr<int>(fmt::format("{}{}", prefix, KeySuffixValue), 0);
        rule.times = config.getOr<int>(fmt::format("{}{}", prefix, KeySuffixTimes), 1);
        return rule;
    }

    /**
     * @brief Builds a RouteRule from this parsed configuration.
     */
    [[nodiscard]] RouteRule toRouteRule() const
    {
        std::unique_ptr<IRouteCondition> condition;

        // If an inline expression string (expr) is present, parse it
        if (!expr.empty()) {
            condition = ConditionExpressionParser::parse(expr);
        }

        // Fallback to legacy/individual fields if expr was omitted or failed
        if (!condition) {
            if (type == "prefix") {
                condition = std::make_unique<TextCondition>(pattern, TextMatchMode::Prefix, 1);
            } else if (type == "exact") {
                condition = std::make_unique<TextCondition>(pattern, TextMatchMode::Exact, 1);
            } else if (type == "client_port" || type == "port") {
                condition = std::make_unique<PortCondition>(static_cast<std::uint16_t>(numericValue));
            } else if (type == "protocol") {
                condition = std::make_unique<ProtocolCondition>(Backend::ProtocolId { static_cast<std::uint8_t>(numericValue) });
            } else if (type == "default" || type == "fallback") {
                condition = std::make_unique<TextCondition>("", TextMatchMode::Contains, 0);
            } else {
                // default contains (with optional times)
                condition = std::make_unique<TextCondition>(pattern, TextMatchMode::Contains, times > 0 ? static_cast<std::size_t>(times) : 1);
            }
        }

        RouteAction action = (actionType == "reject")
            ? RouteAction::reject(rejectReason, rejectErrorCode)
            : RouteAction::forward(target);

        return RouteRule(std::move(condition), std::move(action));
    }
};

/**
 * @brief Strongly-typed configuration section for RouteHandler.
 */
struct RouteHandlerConfig : public Core::Config::ConfigSection {
    Core::Config::ConfigItem<std::string> inputChannel { this, "route.input_channel", "route_in" };
    Core::Config::ConfigItem<std::string> defaultOutputChannel { this, "route.default_output", "" };
    Core::Config::ConfigItem<std::string> rulesCsv { this, "route.rules", "" };

    std::vector<RouteRuleConfig> rules;

    /**
     * @brief Parses top-level config items and expands dynamic rule sub-configurations.
     */
    bool loadRules(const Core::Config::Configuration& config)
    {
        if (!load(config)) {
            return false;
        }

        rules.clear();
        std::string_view namesView(rulesCsv.value());
        std::size_t start = 0;

        while (start < namesView.size()) {
            auto pos = namesView.find(',', start);
            auto name = namesView.substr(start, pos == std::string_view::npos ? pos : pos - start);
            start = (pos == std::string_view::npos) ? namesView.size() : pos + 1;

            if (name.empty()) {
                continue;
            }

            auto prefix = fmt::format("route.rule.{}.", name);
            rules.push_back(RouteRuleConfig::fromPrefix(name, prefix, config));
        }

        return true;
    }
};

} // namespace Proxy::Handler
