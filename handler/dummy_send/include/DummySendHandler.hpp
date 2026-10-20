#pragma once

#include "core/handler/IHandler.hpp"
#include <chrono>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Test handler that registers an output channel and repeatedly sends a text blob with a delay.
 */
class DummySendHandler : public Core::Handler::IHandler {
public:
    DummySendHandler() = default;
    DummySendHandler(std::string channelName, std::string messageText, std::chrono::milliseconds delay);
    ~DummySendHandler() override = default;

    /**
     * @brief Configures DummySendHandler and creates the output channel via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If output channel creation succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Periodic step callback executed by IHandler's event loop.
     * Checks if the delay duration has elapsed, and if so, sends the message blob to the output channel.
     */
    void step() override;

private:
    std::string m_channelName;
    std::string m_messageText;
    std::chrono::milliseconds m_delay { 1000 };
    std::chrono::steady_clock::time_point m_lastSendTime { std::chrono::steady_clock::now() };
    ::Backend::IOutputChannel* m_outputChannel { nullptr };
};

} // namespace Proxy::Handler
