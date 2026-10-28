#include "RouteHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/ProtocolId.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <charconv>
#include <unordered_map>

namespace Proxy::Handler {

RouteHandler::~RouteHandler() = default;

bool RouteHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto inputChannelName = config.getOr<std::string>("route.input_channel", "route_in");
    const auto defaultChannelName = config.getOr<std::string>("route.default_output", "");

    auto inChan = backend().createInputChannel(inputChannelName, config);
    if (!inChan) {
        Proxy::Log::error("[RouteHandler] Failed to create input channel '{}'", inputChannelName);
        return false;
    }
    registerInputChannel(std::move(inChan));

    // Register default output if configured
    if (!defaultChannelName.empty()) {
        auto defChan = backend().createOutputChannel(defaultChannelName, config);
        if (defChan) {
            m_defaultOutputChannel = registerOutputChannel(std::move(defChan));
        } else {
            Proxy::Log::warn("[RouteHandler] Failed to create default output channel '{}'", defaultChannelName);
        }
    }

    // Parse configured rules:
    // e.g.
    // route.rules = rule1,rule2
    // route.rule.rule1.type = payload_contains
    // route.rule.rule1.pattern = VIP
    // route.rule.rule1.target = vip_orders
    auto ruleNames = config.getOr<std::string>("route.rules", "");
    if (!ruleNames.empty()) {
        std::string_view namesView(ruleNames);
        std::size_t start = 0;
        std::unordered_map<std::string, Common::Memory::ObserverPtr<::Backend::IOutputChannel>> createdChannels;

        while (start < namesView.size()) {
            auto pos = namesView.find(',', start);
            auto name = namesView.substr(start, pos == std::string_view::npos ? pos : pos - start);
            start = (pos == std::string_view::npos) ? namesView.size() : pos + 1;

            if (name.empty()) {
                continue;
            }

            std::string prefix = std::string("route.rule.") + std::string(name) + ".";
            std::string typeStr = config.getOr<std::string>(prefix + "type", "contains");
            std::string pattern = config.getOr<std::string>(prefix + "pattern", "");
            std::string target = config.getOr<std::string>(prefix + "target", "");
            int numVal = config.getOr<int>(prefix + "value", 0);

            if (target.empty()) {
                Proxy::Log::warn("[RouteHandler] Rule '{}' has no target channel specified, skipping", name);
                continue;
            }

            RoutePattern patternVariant;
            if (typeStr == "prefix") {
                patternVariant = MatchCriteria::PayloadPrefix { std::move(pattern) };
            } else if (typeStr == "client_port" || typeStr == "port") {
                patternVariant = MatchCriteria::ClientPort { static_cast<std::uint16_t>(numVal) };
            } else if (typeStr == "protocol") {
                patternVariant = MatchCriteria::Protocol { static_cast<Backend::ProtocolId>(numVal) };
            } else if (typeStr == "default" || typeStr == "fallback") {
                patternVariant = MatchCriteria::Fallback { };
            } else {
                patternVariant = MatchCriteria::PayloadContains { std::move(pattern) };
            }

            // Create target channel if not yet created
            auto it = createdChannels.find(target);
            Common::Memory::ObserverPtr<::Backend::IOutputChannel> outPtr;
            if (it == createdChannels.end()) {
                auto outChan = backend().createOutputChannel(target, config);
                if (outChan) {
                    outPtr = registerOutputChannel(std::move(outChan));
                    createdChannels[target] = outPtr;
                } else {
                    Proxy::Log::error("[RouteHandler] Failed to create target output channel '{}' for rule '{}'", target, name);
                }
            } else {
                outPtr = it->second;
            }

            RouteRule rule(std::move(patternVariant), target);
            rule.bindTargetChannel(outPtr);
            m_rules.push_back(std::move(rule));
        }
    }

    return true;
}

void RouteHandler::addRule(RouteRule rule)
{
    m_rules.push_back(std::move(rule));
}

void RouteHandler::setDefaultChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept
{
    m_defaultOutputChannel = channel;
}

const std::vector<RouteRule>& RouteHandler::rules() const noexcept
{
    return m_rules;
}

Backend::MessageResult RouteHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
{
    for (const auto& rule : m_rules) {
        if (rule.matches(message)) {
            auto target = rule.targetChannel();
            if (target) {
                target->send(message.rawBuffer());
                return Backend::Forwarded { .channel = rule.targetChannelName() };
            }
        }
    }

    if (m_defaultOutputChannel) {
        m_defaultOutputChannel->send(message.rawBuffer());
        return Backend::Forwarded { .channel = m_defaultOutputChannel->name() };
    }

    Proxy::Log::debug("[RouteHandler] Message from '{}' matched no rule and no default channel configured. Discarding.", channelName);
    return Backend::Dropped {
        .reason = "Matched no routing rule and no default channel configured",
        .errorCode = 404
    };
}

void RouteHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    if (messages.empty())
        return;

    // Fast path: if there is only 1 rule or all match default, optimize.
    // Otherwise bucket messages by target channel.
    std::unordered_map<Common::Memory::ObserverPtr<::Backend::IOutputChannel>, std::vector<Common::Compat::span<const std::byte>>> buckets;
    const auto count = std::min(messages.size(), outResults.size());

    for (std::size_t i = 0; i < count; ++i) {
        const auto& msg = messages[i];
        Common::Memory::ObserverPtr<::Backend::IOutputChannel> destination;
        std::string_view destinationName;
        for (const auto& rule : m_rules) {
            if (rule.matches(msg)) {
                destination = rule.targetChannel();
                if (destination) {
                    destinationName = rule.targetChannelName();
                }
                break;
            }
        }

        if (!destination) {
            destination = m_defaultOutputChannel;
            if (destination) {
                destinationName = destination->name();
            }
        }

        if (destination) {
            buckets[destination].push_back(Common::Memory::ByteView(msg.rawBuffer()));
            outResults[i] = Backend::Forwarded { .channel = destinationName };
        } else {
            Proxy::Log::debug("[RouteHandler] Message from '{}' matched no rule and no default channel configured. Discarding.", channelName);
            outResults[i] = Backend::Dropped {
                .reason = "Matched no routing rule and no default channel configured",
                .errorCode = 404
            };
        }
    }

    for (auto& [chan, rawVec] : buckets) {
        if (chan && !rawVec.empty()) {
            Common::Compat::span<const Common::Compat::span<const std::byte>> batchSpan(rawVec.data(), rawVec.size());
            chan->sendBatch(batchSpan);
        }
    }
}

} // namespace Proxy::Handler
