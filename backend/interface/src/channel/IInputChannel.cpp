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
        return Common::Net::BatchResult::failure({ 0, Common::Net::ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }

    Common::Net::BytesResult firstResult = receiveSlot(slots[0], timeout);
    if (!firstResult.hasValue()) {
        return Common::Net::BatchResult::failure({ 0, firstResult.error() });
    }
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        Common::Net::BytesResult nextResult = receiveSlot(slots[count], zeroTimeout);
        if (!nextResult.hasValue()) {
            if (!nextResult.error().is<Common::Net::ReceiveTimeout>()) {
                return Common::Net::BatchResult::failure({ count, nextResult.error() });
            }
            break;
        }
        ++count;
    }
    return Common::Net::BatchResult::success(count);
}

} // namespace Backend
