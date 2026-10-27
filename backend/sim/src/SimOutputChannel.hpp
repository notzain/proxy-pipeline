#pragma once

#include "backend/IOutputChannel.hpp"
#include <cstdint>
#include <string>

namespace Proxy::Backend::Sim {

/**
 * @brief Simulation output channel transmitting across IPC and emitting egress events to the dashboard.
 */
class SimOutputChannel : public ::Backend::IOutputChannel {
public:
    SimOutputChannel(
        std::string channelName,
        std::string destSocketPath,
        std::string eventHost,
        int eventPort,
        std::string handlerName);

    ~SimOutputChannel() override;

    bool send(Common::Compat::span<const std::byte> message) override;
    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override;

private:
    void emitEvent(Common::Compat::span<const std::byte> message, std::string_view status);

    std::string m_channelName;
    std::string m_destSocketPath;
    std::string m_eventHost;
    int m_eventPort { 8092 };
    std::string m_handlerName;

    int m_clientFd { -1 };
    int m_eventFd { -1 };
};

} // namespace Proxy::Backend::Sim
