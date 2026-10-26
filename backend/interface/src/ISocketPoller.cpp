#include "backend/ISocketPoller.hpp"

namespace Backend {

std::size_t ISocketPoller::receiveBatch(
    int socketFd,
    std::span<BufferSlot> slots,
    Timeout timeout,
    std::span<SocketAddress> outSourceAddresses)
{
    if (slots.empty()) {
        return 0;
    }

    SocketAddress* firstAddr = (!outSourceAddresses.empty()) ? &outSourceAddresses[0] : nullptr;
    std::size_t firstBytes = receive(socketFd, slots[0].buffer, timeout, firstAddr);
    if (firstBytes == 0) {
        slots[0].size = 0;
        return 0;
    }
    slots[0].size = firstBytes;
    std::size_t count = 1;

    constexpr Timeout zeroTimeout { 0 };
    while (count < slots.size()) {
        SocketAddress* nextAddr = (count < outSourceAddresses.size()) ? &outSourceAddresses[count] : nullptr;
        std::size_t nextBytes = receive(socketFd, slots[count].buffer, zeroTimeout, nextAddr);
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
