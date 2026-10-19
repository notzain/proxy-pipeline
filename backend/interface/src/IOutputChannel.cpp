#include "backend/IOutputChannel.hpp"

namespace Backend {

std::size_t IOutputChannel::sendBatch(std::span<const std::span<const std::byte>> messages)
{
    std::size_t count = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++count;
        }
    }
    return count;
}

} // namespace Backend
