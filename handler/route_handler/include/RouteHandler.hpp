#pragma once

#include "RouteRule.hpp"
#include "core/handler/IHandler.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Dynamic content-based router handler (Enterprise Integration Pattern).
 *
 * Inspects incoming message metadata and payload contents, dynamically dispatching
 * each message to its configured destination output channel.
 *
 * Supports vectorized batch dispatching: groups batch elements by target channel
 * and issues single vectorized sendBatch() operations per destination.
 */
class RouteHandler : public Core::Handler::IHandler {
public:
    RouteHandler() = default;
    ~RouteHandler() override;

    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    void onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;
    void onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages) override;

    /**
     * @brief Adds an explicit routing rule programmatically.
     */
    void addRule(RouteRule rule);

    /**
     * @brief Sets an explicit default fallback channel.
     */
    void setDefaultChannel(::Backend::IOutputChannel* channel) noexcept;

    /**
     * @brief Accessor for configured rules.
     */
    [[nodiscard]] const std::vector<RouteRule>& rules() const noexcept;

private:
    std::string m_inputChannelName;
    std::string m_defaultChannelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
    ::Backend::IOutputChannel* m_defaultOutputChannel { nullptr };
    std::vector<RouteRule> m_rules;
};

} // namespace Proxy::Handler
