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

    // Fast-path: speculative non-blocking read avoids poll() syscall when data is already available
    ssize_t directBytes = ::recvfrom(
        socketFd,
        reinterpret_cast<char*>(destination.data()),
        destination.size(),
        MSG_DONTWAIT,
        nullptr,
        nullptr);

    if (directBytes > 0) {
        return static_cast<std::size_t>(directBytes);
    }

    if (timeout.count() == 0) {
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
            MSG_DONTWAIT,
            nullptr,
            nullptr);

        if (bytesRead > 0) {
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

std::size_t PosixSocketPoller::receiveBatch(
    int socketFd,
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (socketFd < 0 || destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    std::size_t count = 0;
    const std::size_t maxBatch = destinations.size();

    // Fast-path: speculative non-blocking drain
    while (count < maxBatch) {
        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destinations[count].data()),
            destinations[count].size(),
            MSG_DONTWAIT,
            nullptr,
            nullptr);

        if (bytes > 0) {
            outSizes[count++] = static_cast<std::size_t>(bytes);
        } else {
            break;
        }
    }

    if (count > 0 || timeout.count() == 0) {
        return count;
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);

    if (ret > 0 && (pfd.revents & POLLIN)) {
        while (count < maxBatch) {
            ssize_t bytes = ::recvfrom(
                socketFd,
                reinterpret_cast<char*>(destinations[count].data()),
                destinations[count].size(),
                MSG_DONTWAIT,
                nullptr,
                nullptr);

            if (bytes > 0) {
                outSizes[count++] = static_cast<std::size_t>(bytes);
            } else {
                break;
            }
        }
    }

    return count;
}

} // namespace Backend
