#include "PosixSocketPoller.hpp"
#include "log/Log.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

namespace Proxy::Backend::Socket {

std::size_t PosixSocketPoller::receive(int socketFd, std::span<std::byte> destination, Timeout timeout)
{
    if (socketFd < 0 || destination.empty()) {
        return 0;
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);

    if (ret > 0 && (pfd.revents & POLLIN)) {
        sockaddr_in clientAddr { };
        socklen_t clientLen = sizeof(clientAddr);

        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen);

        if (bytesRead > 0) {
            char clientIp[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
            Proxy::Log::debug("[PosixSocketPoller] Received {} bytes from {}:{}", bytesRead, clientIp, ntohs(clientAddr.sin_port));
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Socket
