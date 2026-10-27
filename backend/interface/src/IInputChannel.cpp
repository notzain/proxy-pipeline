#include "backend/IInputChannel.hpp"
#include <system_error>
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

ReceiveMessageResult IInputChannel::receive(Timeout timeout)
{
    Buffer buffer(65535);
    BytesResult result = receiveInto(Common::Compat::span<std::byte>(buffer.data(), buffer.size()), timeout);
    if (!result.hasValue()) {
        return ReceiveMessageResult::failure(result.error());
    }
    buffer.resize(result.value());
    return ReceiveMessageResult::success(std::move(buffer));
}

BatchResult IInputChannel::receiveBatch(
    Common::Compat::span<BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty()) {
        return BatchResult::failure({ 0, receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
    }

    BytesResult firstResult = receiveInto(slots[0].buffer, timeout);
    if (!firstResult.hasValue()) {
        slots[0].size = 0;
        return BatchResult::failure({ 0, firstResult.error() });
    }
    slots[0].size = firstResult.value();
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        BytesResult nextResult = receiveInto(slots[count].buffer, zeroTimeout);
        if (!nextResult.hasValue()) {
            slots[count].size = 0;
            if (!isReceiveTimeout(nextResult.error())) {
                return BatchResult::failure({ count, nextResult.error() });
            }
            break;
        }
        slots[count].size = nextResult.value();
        ++count;
    }
    return BatchResult::success(count);
}

} // namespace Backend
