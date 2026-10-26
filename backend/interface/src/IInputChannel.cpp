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
    std::span<BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty()) {
        return 0;
    }

    std::size_t firstBytes = receiveInto(slots[0].buffer, timeout);
    if (firstBytes == 0) {
        slots[0].size = 0;
        return 0;
    }
    slots[0].size = firstBytes;
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        std::size_t nextBytes = receiveInto(slots[count].buffer, zeroTimeout);
        if (nextBytes == 0) {
            slots[count].size = 0;
            break;
        }
        slots[count].size = nextBytes;
        ++count;
    }
    return count;
}

} // namespace Backend
