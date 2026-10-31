#include "common/net/ISocketPoller.hpp"
#include <system_error>

namespace Common::Net {

BatchResult ISocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<PollerBufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    if (slots.empty()) {
        return BatchResult::failure({ 0, ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }

    SocketAddress* firstAddr = (!outSourceAddresses.empty()) ? &outSourceAddresses[0] : nullptr;
    BytesResult firstResult = receive(socketFd, slots[0].buffer, timeout, firstAddr);
    if (!firstResult.hasValue()) {
        slots[0].size = 0;
        return BatchResult::failure({ 0, firstResult.error() });
    }
    slots[0].size = firstResult.value();
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        SocketAddress* nextAddr = (count < outSourceAddresses.size()) ? &outSourceAddresses[count] : nullptr;
        BytesResult nextResult = receive(socketFd, slots[count].buffer, zeroTimeout, nextAddr);
        if (!nextResult.hasValue()) {
            slots[count].size = 0;
            if (!nextResult.error().is<ReceiveTimeout>()) {
                return BatchResult::failure({ count, nextResult.error() });
            }
            break;
        }
        slots[count].size = nextResult.value();
        ++count;
    }
    return BatchResult::success(count);
}

} // namespace Common::Net
