#include "PosixUdsPoller.hpp"
#include "log/Log.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace Proxy::Backend::Uds {

std::size_t PosixUdsPoller::receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout)
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
        sockaddr_un clientAddr { };
        socklen_t clientLen = sizeof(clientAddr);

        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen);

        if (bytesRead > 0) {
            Proxy::Log::debug("[PosixUdsPoller] Received {} bytes on UDS", bytesRead);
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Uds
