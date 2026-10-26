#include "backend/uds/UdsInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include "log/Log.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Uds {

UdsInputChannel::UdsInputChannel(std::string name, std::string socketPath)
    : ::Backend::IInputChannel(std::move(name))
    , m_socketPath(std::move(socketPath))
    , m_poller(::Backend::createSocketPoller())
{
    openAndBindSocket();
}

UdsInputChannel::~UdsInputChannel()
{
    closeSocket();
}

void UdsInputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
    if (!m_socketPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_socketPath, ec);
    }
}

void UdsInputChannel::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[UdsInputChannel] Failed to create UDS socket: {}", std::strerror(errno));
        return;
    }

    int rcvbuf = 8 * 1024 * 1024; // 8MB receive buffer for high-throughput bursts
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    if (m_socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
        Proxy::Log::error("[UdsInputChannel] Socket path too long: {}", m_socketPath);
        closeSocket();
        return;
    }

    // Ensure parent directory exists
    std::filesystem::path p(m_socketPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // Unlink old socket file if it exists
    ::unlink(m_socketPath.c_str());

    sockaddr_un addr { };
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Proxy::Log::error("[UdsInputChannel] Failed to bind UDS socket to {}: {}", m_socketPath, std::strerror(errno));
        closeSocket();
        return;
    }

    Proxy::Log::info("[UdsInputChannel] Listening on UDS path {}", m_socketPath);
}

std::size_t UdsInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return 0;
        }
    }

    if (m_poller) {
        return m_poller->receive(m_socketFd, destination, timeout);
    }

    return 0;
}

std::size_t UdsInputChannel::receiveBatch(
    std::span<::Backend::BufferSlot> slots,
    Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return 0;
        }
    }

    if (m_poller) {
        return m_poller->receiveBatch(m_socketFd, slots, timeout);
    }

    return 0;
}

} // namespace Proxy::Backend::Uds
