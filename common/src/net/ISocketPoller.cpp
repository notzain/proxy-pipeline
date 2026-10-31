#include "common/net/ISocketPoller.hpp"
#include <system_error>

namespace Common::Net {

PollerBatchResult ISocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<PollerBufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    if (slots.empty()) {
        return PollerBatchResult::failure({ 0, pollerFailure(std::make_error_code(std::errc::invalid_argument)) });
    }

    SocketAddress* firstAddr = (!outSourceAddresses.empty()) ? &outSourceAddresses[0] : nullptr;
    PollerBytesResult firstResult = receive(socketFd, slots[0].buffer, timeout, firstAddr);
    if (!firstResult.hasValue()) {
        slots[0].size = 0;
        return PollerBatchResult::failure({ 0, firstResult.error() });
    }
    slots[0].size = firstResult.value();
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        SocketAddress* nextAddr = (count < outSourceAddresses.size()) ? &outSourceAddresses[count] : nullptr;
        PollerBytesResult nextResult = receive(socketFd, slots[count].buffer, zeroTimeout, nextAddr);
        if (!nextResult.hasValue()) {
            slots[count].size = 0;
            if (!isPollerTimeout(nextResult.error())) {
                return PollerBatchResult::failure({ count, nextResult.error() });
            }
            break;
        }
        slots[count].size = nextResult.value();
        ++count;
    }
    return PollerBatchResult::success(count);
}

} // namespace Common::Net
