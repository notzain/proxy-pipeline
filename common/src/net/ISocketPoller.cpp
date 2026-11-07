#include "common/net/ISocketPoller.hpp"
#include <system_error>
#include <variant>

namespace Common::Net {

namespace {

    bool isMessageTooLarge(const ReceiveError& error)
    {
        const auto* failure = std::get_if<ReceiveFailure>(&error);
        return failure && failure->code == std::make_error_code(std::errc::message_size);
    }

} // namespace

BatchResult ISocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<PollerBufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    if (slots.empty()) {
        return BatchResult::err({ 0, ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }

    SocketAddress* firstAddr = (!outSourceAddresses.empty()) ? &outSourceAddresses[0] : nullptr;
    BytesResult firstResult = receive(socketFd, slots[0].buffer, timeout, firstAddr);
    if (!firstResult.isOk()) {
        slots[0].size = 0;
        slots[0].truncated = isMessageTooLarge(firstResult.unwrapErr());
        return BatchResult::err({ 0, firstResult.unwrapErr() });
    }
    slots[0].size = firstResult.unwrap();
    slots[0].truncated = false;
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        SocketAddress* nextAddr = (count < outSourceAddresses.size()) ? &outSourceAddresses[count] : nullptr;
        BytesResult nextResult = receive(socketFd, slots[count].buffer, zeroTimeout, nextAddr);
        if (!nextResult.isOk()) {
            slots[count].size = 0;
            slots[count].truncated = isMessageTooLarge(nextResult.unwrapErr());
            if (!nextResult.unwrapErr().is<ReceiveTimeout>()) {
                return BatchResult::err({ count, nextResult.unwrapErr() });
            }
            break;
        }
        slots[count].size = nextResult.unwrap();
        slots[count].truncated = false;
        ++count;
    }
    return BatchResult::ok(count);
}

} // namespace Common::Net
