#pragma once

#include "core/IHandler.hpp"
#include <chrono>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Test handler that registers an output channel and repeatedly sends a text blob with a delay.
 */
class DummySendHandler : public Core::IHandler {
public:
    DummySendHandler(std::string channelName, std::string messageText, std::chrono::milliseconds delay);
    ~DummySendHandler() override = default;

    /**
     * @brief Periodic step callback executed by IHandler's event loop.
     * Checks if the delay duration has elapsed, and if so, sends the message blob to the output channel.
     */
    void step() override;

private:
    std::string m_channelName;
    std::string m_messageText;
    std::chrono::milliseconds m_delay;
    std::chrono::steady_clock::time_point m_lastSendTime;
};

} // namespace Proxy::Handler
