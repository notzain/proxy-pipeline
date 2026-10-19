#include "PosixSocketPoller.hpp"
#include <chrono>
#include <poll.h>
#include <sys/socket.h>

namespace Backend {

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
        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            nullptr,
            nullptr);

        if (bytesRead > 0) {
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

} // namespace Backend
