#pragma once

#include "backend/IInputChannel.hpp"
#include <cstdint>
#include <string>

namespace Proxy::Backend::Sim {

/**
 * @brief Simulation input channel supporting IPC ingestion and event emission.
 */
class SimInputChannel : public ::Backend::IInputChannel {
public:
    SimInputChannel(
        std::string channelName,
        std::string socketPath,
        std::string eventHost,
        int eventPort,
        std::string handlerName);

    ~SimInputChannel() override;

    std::optional<Buffer> receive(Timeout timeout) override;
    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;
    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout) override;

private:
    void emitEvent(std::span<const std::byte> payload, std::string_view status);

    std::string m_channelName;
    std::string m_socketPath;
    std::string m_eventHost;
    int m_eventPort { 8092 };
    std::string m_handlerName;

    int m_serverFd { -1 };
    int m_eventFd { -1 };
};

} // namespace Proxy::Backend::Sim
