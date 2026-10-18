#pragma once

#include "backend/IOutputChannel.hpp"
#include <cstdint>
#include <netinet/in.h>
#include <span>
#include <string>

namespace Proxy::Backend::Socket {

/**
 * @brief Output channel for transmitting UDP datagrams to a destination host and port.
 */
class SocketOutputChannel : public ::Backend::IOutputChannel {
public:
    SocketOutputChannel(std::string destHost, std::uint16_t destPort);
    ~SocketOutputChannel() override;

    bool send(std::span<const std::byte> message) override;

private:
    std::string m_destHost;
    std::uint16_t m_destPort;
    sockaddr_in m_destAddr { };
    int m_socketFd { -1 };

    void closeSocket();
    void openSocket();
};

} // namespace Proxy::Backend::Socket
