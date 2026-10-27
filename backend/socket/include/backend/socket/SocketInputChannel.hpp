#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace Proxy::Backend::Socket {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

/**
 * @brief Input channel for receiving UDP datagrams on a bound host and port.
 */
class SocketInputChannel : public ::Backend::IInputChannel {
public:
    SocketInputChannel(std::string name, std::string host, std::uint16_t port);
    ~SocketInputChannel() override;

    BytesResult receiveInto(std::span<std::byte> destination, Timeout timeout) override;
    BatchResult receiveBatch(
        std::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    std::string m_host;
    std::uint16_t m_port;
    int m_socketFd { -1 };
    std::unique_ptr<::Backend::ISocketPoller> m_poller;

    void closeSocket();
    void openAndBindSocket();
};

} // namespace Proxy::Backend::Socket
