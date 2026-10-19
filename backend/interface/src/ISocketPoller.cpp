#include "backend/ISocketPoller.hpp"

namespace Backend {

std::size_t ISocketPoller::receiveBatch(
    int socketFd,
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    std::size_t firstBytes = receive(socketFd, destinations[0], timeout);
    if (firstBytes == 0) {
        return 0;
    }
    outSizes[0] = firstBytes;
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < destinations.size()) {
        std::size_t nextBytes = receive(socketFd, destinations[count], zeroTimeout);
        if (nextBytes == 0) {
            break;
        }
        outSizes[count] = nextBytes;
        ++count;
    }
    return count;
}

} // namespace Backend

