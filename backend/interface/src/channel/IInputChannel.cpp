#include "backend/channel/IInputChannel.hpp"
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

bool IInputChannel::providesOwnMemory() const noexcept
{
    return false;
}

Common::Net::BatchResult IInputChannel::receiveBatch(
    Common::Compat::span<BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty()) {
        return Common::Net::BatchResult::err({ 0, Common::Net::ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }

    Common::Net::BytesResult firstResult = receiveSlot(slots[0], timeout);
    if (!firstResult.isOk()) {
        return Common::Net::BatchResult::err({ 0, firstResult.unwrapErr() });
    }
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        Common::Net::BytesResult nextResult = receiveSlot(slots[count], zeroTimeout);
        if (!nextResult.isOk()) {
            if (!nextResult.unwrapErr().is<Common::Net::ReceiveTimeout>()) {
                return Common::Net::BatchResult::err({ count, nextResult.unwrapErr() });
            }
            break;
        }
        ++count;
    }
    return Common::Net::BatchResult::ok(count);
}

} // namespace Backend
