#include "backend/IInputChannel.hpp"
#include <algorithm>
#include <cstring>

namespace Backend {

std::size_t IInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    auto msg = receive(timeout);
    if (!msg.has_value() || msg->empty()) {
        return 0;
    }
    const std::size_t bytesToCopy = std::min(destination.size(), msg->size());
    std::memcpy(destination.data(), msg->data(), bytesToCopy);
    return bytesToCopy;
}

std::size_t IInputChannel::receiveBatch(
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    std::size_t firstBytes = receiveInto(destinations[0], timeout);
    if (firstBytes == 0) {
        return 0;
    }
    outSizes[0] = firstBytes;
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < destinations.size()) {
        std::size_t nextBytes = receiveInto(destinations[count], zeroTimeout);
        if (nextBytes == 0) {
            break;
        }
        outSizes[count] = nextBytes;
        ++count;
    }
    return count;
}

} // namespace Backend
