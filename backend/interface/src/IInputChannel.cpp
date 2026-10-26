#include "backend/IInputChannel.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

namespace Backend {

IInputChannel::IInputChannel(std::string name)
    : m_name(std::move(name))
{
}

std::string_view IInputChannel::name() const noexcept
{
    return m_name;
}

std::optional<IInputChannel::Buffer> IInputChannel::receive(Timeout timeout)
{
    Buffer buffer(65535);
    std::size_t bytes = receiveInto(std::span<std::byte>(buffer.data(), buffer.size()), timeout);
    if (bytes == 0) {
        return std::nullopt;
    }
    buffer.resize(bytes);
    return buffer;
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
