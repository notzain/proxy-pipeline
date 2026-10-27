#pragma once

#include "backend/IInputChannel.hpp"
#include <string>

namespace Proxy::Backend::Sim {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

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

    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout) override;
    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    void emitEvent(Common::Compat::span<const std::byte> payload, std::string_view status);

    std::string m_channelName;
    std::string m_socketPath;
    std::string m_eventHost;
    int m_eventPort { 8092 };
    std::string m_handlerName;

    int m_serverFd { -1 };
    int m_eventFd { -1 };
};

} // namespace Proxy::Backend::Sim
