#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Uds {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

/**
 * @brief Input channel for receiving datagrams on a bound Unix Domain Socket.
 */
class UdsInputChannel : public ::Backend::IInputChannel {
public:
    UdsInputChannel(std::string name, std::string socketPath);
    ~UdsInputChannel() override;

    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout) override;
    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    std::string m_socketPath;
    int m_socketFd { -1 };
    std::unique_ptr<::Backend::ISocketPoller> m_poller;

    void closeSocket();
    void openAndBindSocket();
};

} // namespace Proxy::Backend::Uds
