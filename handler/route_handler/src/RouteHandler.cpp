#include "RouteHandler.hpp"
#include "RouteConfig.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <unordered_map>

namespace Proxy::Handler {

RouteHandler::~RouteHandler() = default;

bool RouteHandler::onConfigure(const Core::Config::Configuration& config)
{
    RouteHandlerConfig cfg;
    if (!cfg.loadRules(config)) {
        PROXY_LOG_ERROR("Failed to load configuration");
        return false;
    }

    auto inChan = backend().createInputChannel(cfg.inputChannel.value(), config);
    if (!inChan) {
        PROXY_LOG_ERROR("Failed to create input channel '{}'", cfg.inputChannel);
        return false;
    }
    registerInputChannel(std::move(inChan));

    // Register default output if configured
    if (!cfg.defaultOutputChannel->empty()) {
        auto defChan = backend().createOutputChannel(cfg.defaultOutputChannel.value(), config);
        if (defChan) {
            m_defaultOutputChannel = registerOutputChannel(std::move(defChan));
        } else {
            PROXY_LOG_WARN("Failed to create default output channel '{}'", cfg.defaultOutputChannel);
        }
    }

    // Configure parsed rules
    if (!cfg.rules.empty()) {
        std::unordered_map<std::string, Common::Memory::ObserverPtr<::Backend::IOutputChannel>> createdChannels;

        for (const auto& ruleCfg : cfg.rules) {
            auto rule = ruleCfg.toRouteRule();

            if (rule.action().isForward()) {
                const auto& target = rule.targetChannelName();
                if (target.empty()) {
                    PROXY_LOG_WARN("Forward rule '{}' has no target channel specified, skipping", ruleCfg.name);
                    continue;
                }

                auto it = createdChannels.find(target);
                Common::Memory::ObserverPtr<::Backend::IOutputChannel> outPtr;
                if (it == createdChannels.end()) {
                    auto outChan = backend().createOutputChannel(target, config);
                    if (outChan) {
                        outPtr = registerOutputChannel(std::move(outChan));
                        createdChannels[target] = outPtr;
                    } else {
                        PROXY_LOG_ERROR("Failed to create target output channel '{}' for rule '{}'", target, ruleCfg.name);
                    }
                } else {
                    outPtr = it->second;
                }

                rule.bindTargetChannel(outPtr);
            }

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
    for (auto& rule : m_rules) {
        if (!rule.matches(message)) {
            continue;
        }

        if (const auto* rej = rule.action().asReject()) {
            PROXY_LOG_DEBUG("Message from '{}' explicitly rejected: {}", channelName, rej->reason);
            return Backend::Dropped {
                .reason = rej->reason,
                .errorCode = rej->errorCode
            };
        }

        if (auto target = rule.targetChannel()) {
            target->send(message);
            return Backend::Forwarded { .channel = rule.targetChannelName() };
        }
    }

    if (m_defaultOutputChannel) {
        m_defaultOutputChannel->send(message);
        return Backend::Forwarded { .channel = m_defaultOutputChannel->name() };
    }

    PROXY_LOG_DEBUG("Message from '{}' matched no rule and no default channel configured. Discarding.", channelName);
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
    if (messages.empty()) {
        return;
    }

    std::unordered_map<Common::Memory::ObserverPtr<::Backend::IOutputChannel>, std::vector<Backend::MessageView>> buckets;
    const auto count = std::min(messages.size(), outResults.size());

    for (std::size_t i = 0; i < count; ++i) {
        const auto& msg = messages[i];
        bool handled = false;

        for (auto& rule : m_rules) {
            if (!rule.matches(msg)) {
                continue;
            }

            if (const auto* rej = rule.action().asReject()) {
                outResults[i] = Backend::Dropped {
                    .reason = rej->reason,
                    .errorCode = rej->errorCode
                };
                handled = true;
                break;
            }

            auto target = rule.targetChannel();
            if (target) {
                buckets[target].push_back(msg);
                outResults[i] = Backend::Forwarded { .channel = rule.targetChannelName() };
                handled = true;
                break;
            }
        }

        if (handled) {
            continue;
        }

        if (m_defaultOutputChannel) {
            buckets[m_defaultOutputChannel].push_back(msg);
            outResults[i] = Backend::Forwarded { .channel = m_defaultOutputChannel->name() };
        } else {
            PROXY_LOG_DEBUG("Message from '{}' matched no rule and no default channel configured. Discarding.", channelName);
            outResults[i] = Backend::Dropped {
                .reason = "Matched no routing rule and no default channel configured",
                .errorCode = 404
            };
        }
    }

    for (auto& [chan, viewVec] : buckets) {
        if (chan && !viewVec.empty()) {
            Common::Compat::span<const Backend::MessageView> batchSpan(viewVec.data(), viewVec.size());
            chan->sendBatch(batchSpan);
        }
    }
}

} // namespace Proxy::Handler
