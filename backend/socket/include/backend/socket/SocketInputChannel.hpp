#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Proxy::Backend::Socket {

/**
 * @brief Input channel for receiving UDP datagrams on a bound host and port.
 */
class SocketInputChannel : public ::Backend::IInputChannel {
public:
    SocketInputChannel(std::string host, std::uint16_t port);
    ~SocketInputChannel() override;

    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;
    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
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
