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
    using Core::Handler::IHandler::IHandler;
    ~DummySendHandler() override = default;

    /**
     * @brief Configures DummySendHandler and creates the output channel via the backend.
     *
     * @param config Configuration store.
     * @return true If output channel creation succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    [[nodiscard]] Core::Handler::HandlerRole role() const noexcept override { return Core::Handler::HandlerRole::Generator; }

    /**
     * @brief Periodic step callback executed by IHandler's event loop.
     * Checks if the delay duration has elapsed, and if so, sends the message blob to the output channel.
     */
    void step() override;

private:
    std::string m_messageText;
    std::chrono::milliseconds m_delay { 1000 };
    std::chrono::steady_clock::time_point m_lastSendTime { std::chrono::steady_clock::now() };
    Common::Memory::ObserverPtr<::Backend::IOutputChannel> m_outputChannel;
};

} // namespace Proxy::Handler
