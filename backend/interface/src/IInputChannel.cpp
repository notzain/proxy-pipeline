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

BatchResult IInputChannel::receiveBatch(
    Common::Compat::span<BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty()) {
        return BatchResult::failure({ 0, receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
    }

    BytesResult firstResult = receiveSlot(slots[0], timeout);
    if (!firstResult.hasValue()) {
        return BatchResult::failure({ 0, firstResult.error() });
    }
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        BytesResult nextResult = receiveSlot(slots[count], zeroTimeout);
        if (!nextResult.hasValue()) {
            if (!isReceiveTimeout(nextResult.error())) {
                return BatchResult::failure({ count, nextResult.error() });
            }
            break;
        }
        ++count;
    }
    return BatchResult::success(count);
}

} // namespace Backend
