#include "backend/socket/SocketOutputChannel.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Socket {

SocketOutputChannel::SocketOutputChannel(std::string destHost, std::uint16_t destPort)
    : m_destHost(std::move(destHost))
    , m_destPort(destPort)
{
    openSocket();
}

SocketOutputChannel::~SocketOutputChannel()
{
    closeSocket();
}

void SocketOutputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void SocketOutputChannel::openSocket()
{
    closeSocket();
    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[SocketOutputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }
    Proxy::Log::info("[SocketOutputChannel] Target configured for UDP {}:{}", m_destHost, m_destPort);
}

bool SocketOutputChannel::send(std::span<const std::byte> message)
{
    if (m_socketFd < 0) {
        openSocket();
        if (m_socketFd < 0) {
            return false;
        }
    }

    sockaddr_in destAddr { };
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_destPort);

    if (::inet_pton(AF_INET, m_destHost.c_str(), &destAddr.sin_addr) <= 0) {
        Proxy::Log::error("[SocketOutputChannel] Invalid destination IP address: {}", m_destHost);
        return false;
    }

    ssize_t sent = ::sendto(
        m_socketFd,
        reinterpret_cast<const char*>(message.data()),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(&destAddr),
        sizeof(destAddr));

    if (sent < 0) {
        Proxy::Log::error("[SocketOutputChannel] Failed to send {} bytes to {}:{} ({})", message.size(), m_destHost, m_destPort, std::strerror(errno));
        return false;
    }

    Proxy::Log::debug("[SocketOutputChannel] Sent {} bytes to {}:{}", sent, m_destHost, m_destPort);
    return true;
}

} // namespace Proxy::Backend::Socket
