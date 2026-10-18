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

} // namespace Backend

