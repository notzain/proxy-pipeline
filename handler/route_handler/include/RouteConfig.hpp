#pragma once

#include "core/config/ConfigItem.hpp"
#include "core/config/ConfigSection.hpp"
#include "core/config/Configuration.hpp"
#include <fmt/format.h>
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
    int numericValue { 0 };

    static constexpr std::string_view KeySuffixType { "type" };
    static constexpr std::string_view KeySuffixPattern { "pattern" };
    static constexpr std::string_view KeySuffixTarget { "target" };
    static constexpr std::string_view KeySuffixValue { "value" };

    /**
     * @brief Constructs a RouteRuleConfig from the given prefix and configuration store.
     */
    static RouteRuleConfig fromPrefix(std::string_view ruleName,
        std::string_view prefix,
        const Core::Config::Configuration& config)
    {
        RouteRuleConfig rule;
        rule.name = std::string(ruleName);
        rule.type = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixType), "contains");
        rule.pattern = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixPattern), "");
        rule.target = config.getOr<std::string>(fmt::format("{}{}", prefix, KeySuffixTarget), "");
        rule.numericValue = config.getOr<int>(fmt::format("{}{}", prefix, KeySuffixValue), 0);
        return rule;
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
