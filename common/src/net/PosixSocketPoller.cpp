#include "PosixSocketPoller.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>

namespace Common::Net {

namespace {

    BytesResult receiveError(int error)
    {
        if (error == EBADF || error == ENOTSOCK) {
            return BytesResult::err(ReceiveClosed { });
        }
        if (error == EAGAIN || error == EWOULDBLOCK) {
            return BytesResult::err(ReceiveTimeout { });
        }
        return BytesResult::err(ReceiveFailure { std::error_code(error, std::generic_category()) });
    }

    ReceiveFailure messageTooLarge()
    {
        return ReceiveFailure { std::make_error_code(std::errc::message_size) };
    }

    BatchResult batchError(int error, std::size_t receivedCount = 0)
    {
        ReceiveError cause;
        if (error == EBADF || error == ENOTSOCK) {
            cause = ReceiveClosed { };
        } else if (error == EAGAIN || error == EWOULDBLOCK) {
            cause = ReceiveTimeout { };
        } else {
            cause = ReceiveFailure { std::error_code(error, std::generic_category()) };
        }
        return BatchResult::err({ receivedCount, std::move(cause) });
    }

} // namespace

BytesResult PosixSocketPoller::receive(
    int socketFd,
    Common::Compat::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (socketFd < 0) {
        return BytesResult::err(ReceiveClosed { });
    }
    if (destination.empty()) {
        return BytesResult::err(ReceiveFailure { std::make_error_code(std::errc::invalid_argument) });
    }

    auto readDatagram = [&]() -> BytesResult {
        sockaddr_storage ss { };
        iovec iov { destination.data(), destination.size() };
        msghdr msg { };
        msg.msg_name = outSourceAddress ? &ss : nullptr;
        msg.msg_namelen = outSourceAddress ? sizeof(ss) : 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        const ssize_t bytes = ::recvmsg(socketFd, &msg, MSG_DONTWAIT);
        if (bytes >= 0) {
            if ((msg.msg_flags & MSG_TRUNC) != 0 || static_cast<std::size_t>(bytes) > destination.size()) {
                return BytesResult::err(messageTooLarge());
            }
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&ss, msg.msg_namelen);
            }
            return BytesResult::ok(static_cast<std::size_t>(bytes));
        }
        return receiveError(errno);
    };

    auto directResult = readDatagram();
    if (directResult.isOk() || !directResult.unwrapErr().is<ReceiveTimeout>()) {
        return directResult;
    }
    if (timeout.count() <= 0) {
        return directResult;
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret == 0) {
        return BytesResult::err(ReceiveTimeout { });
    }
    if (ret < 0) {
        return receiveError(errno);
    }
    if (pfd.revents & POLLNVAL) {
        return BytesResult::err(ReceiveClosed { });
    }
    if (pfd.revents & POLLHUP) {
        return BytesResult::err(ReceiveClosed { });
    }
    if (pfd.revents & POLLERR) {
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) < 0) {
            return receiveError(errno);
        }
        if (socketError != 0) {
            return receiveError(socketError);
        }
        return BytesResult::err(ReceiveFailure { std::make_error_code(std::errc::io_error) });
    }
    if (!(pfd.revents & POLLIN)) {
        return BytesResult::err(ReceiveFailure { std::make_error_code(std::errc::io_error) });
    }

    return readDatagram();
}

BatchResult PosixSocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<PollerBufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    if (socketFd < 0) {
        return BatchResult::err({ 0, ReceiveClosed { } });
    }
    if (slots.empty()) {
        return BatchResult::err({ 0, ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }
    for (auto& slot : slots) {
        slot.size = 0;
        slot.truncated = false;
    }

    std::size_t count = 0;
    bool truncated = false;
    const std::size_t maxBatch = slots.size();

#if defined(__linux__)
    constexpr std::size_t kernelBatchMax = 32;
    const std::size_t batchTarget = std::min(maxBatch, kernelBatchMax);
    struct mmsghdr msgvec[kernelBatchMax] { };
    struct iovec iov[kernelBatchMax] { };
    sockaddr_storage addrs[kernelBatchMax] { };

    for (std::size_t i = 0; i < batchTarget; ++i) {
        slots[i].size = 0;
        slots[i].truncated = false;
        iov[i].iov_base = slots[i].buffer.data();
        iov[i].iov_len = slots[i].buffer.size();
        msgvec[i].msg_hdr.msg_iov = &iov[i];
        msgvec[i].msg_hdr.msg_iovlen = 1;
        if (i < outSourceAddresses.size()) {
            msgvec[i].msg_hdr.msg_name = &addrs[i];
            msgvec[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
        }
    }

    int received = ::recvmmsg(socketFd, msgvec, static_cast<unsigned int>(batchTarget), MSG_DONTWAIT, nullptr);
    if (received > 0) {
        bool batchTruncated = false;
        for (int i = 0; i < received; ++i) {
            const bool slotTruncated = (msgvec[i].msg_hdr.msg_flags & MSG_TRUNC) != 0
                || msgvec[i].msg_len > iov[i].iov_len;
            slots[i].size = slotTruncated ? 0 : msgvec[i].msg_len;
            slots[i].truncated = slotTruncated;
            batchTruncated = batchTruncated || slotTruncated;
            if (static_cast<std::size_t>(i) < outSourceAddresses.size()) {
                outSourceAddresses[i].setFromNative(&addrs[i], msgvec[i].msg_hdr.msg_namelen);
            }
        }
        if (batchTruncated) {
            return BatchResult::err({ static_cast<std::size_t>(received), messageTooLarge() });
        }
        return BatchResult::ok(static_cast<std::size_t>(received));
    }
    if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return batchError(errno);
    }
#else
    while (count < maxBatch) {
        sockaddr_storage ss { };
        iovec iov { slots[count].buffer.data(), slots[count].buffer.size() };
        msghdr msg { };
        msg.msg_name = count < outSourceAddresses.size() ? &ss : nullptr;
        msg.msg_namelen = count < outSourceAddresses.size() ? sizeof(ss) : 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        const ssize_t bytes = ::recvmsg(socketFd, &msg, MSG_DONTWAIT);
        if (bytes >= 0) {
            const bool slotTruncated = (msg.msg_flags & MSG_TRUNC) != 0
                || static_cast<std::size_t>(bytes) > slots[count].buffer.size();
            if (count < outSourceAddresses.size()) {
                outSourceAddresses[count].setFromNative(&ss, msg.msg_namelen);
            }
            slots[count++].size = slotTruncated ? 0 : static_cast<std::size_t>(bytes);
            slots[count - 1].truncated = slotTruncated;
            truncated = truncated || slotTruncated;
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return batchError(errno, count);
        }
        break;
    }
#endif

    if (count > 0 || timeout.count() <= 0) {
        if (truncated) {
            return BatchResult::err({ count, messageTooLarge() });
        }
        return count > 0
            ? BatchResult::ok(count)
            : BatchResult::err({ 0, ReceiveTimeout { } });
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret == 0) {
        return BatchResult::err({ 0, ReceiveTimeout { } });
    }
    if (ret < 0) {
        return batchError(errno);
    }
    if (pfd.revents & POLLNVAL || pfd.revents & POLLHUP) {
        return BatchResult::err({ 0, ReceiveClosed { } });
    }
    if (pfd.revents & POLLERR) {
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) < 0) {
            return batchError(errno);
        }
        return socketError != 0
            ? batchError(socketError)
            : BatchResult::err({ 0, ReceiveFailure { std::make_error_code(std::errc::io_error) } });
    }
    if (!(pfd.revents & POLLIN)) {
        return BatchResult::err({ 0, ReceiveFailure { std::make_error_code(std::errc::io_error) } });
    }

#if defined(__linux__)
    for (std::size_t i = 0; i < batchTarget; ++i) {
        msgvec[i].msg_hdr.msg_flags = 0;
        msgvec[i].msg_hdr.msg_namelen = i < outSourceAddresses.size() ? sizeof(addrs[i]) : 0;
    }
    received = ::recvmmsg(socketFd, msgvec, static_cast<unsigned int>(batchTarget), MSG_DONTWAIT, nullptr);
    if (received < 0) {
        return batchError(errno);
    }
    for (int i = 0; i < received; ++i) {
        const bool slotTruncated = (msgvec[i].msg_hdr.msg_flags & MSG_TRUNC) != 0
            || msgvec[i].msg_len > iov[i].iov_len;
        slots[i].size = slotTruncated ? 0 : msgvec[i].msg_len;
        slots[i].truncated = slotTruncated;
        truncated = truncated || slotTruncated;
        if (static_cast<std::size_t>(i) < outSourceAddresses.size()) {
            outSourceAddresses[i].setFromNative(&addrs[i], msgvec[i].msg_hdr.msg_namelen);
        }
    }
    if (truncated) {
        return BatchResult::err({ static_cast<std::size_t>(received), messageTooLarge() });
    }
    return received > 0
        ? BatchResult::ok(static_cast<std::size_t>(received))
        : BatchResult::err({ 0, ReceiveTimeout { } });
#else
    while (count < maxBatch) {
        sockaddr_storage ss { };
        iovec iov { slots[count].buffer.data(), slots[count].buffer.size() };
        msghdr msg { };
        msg.msg_name = count < outSourceAddresses.size() ? &ss : nullptr;
        msg.msg_namelen = count < outSourceAddresses.size() ? sizeof(ss) : 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        const ssize_t bytes = ::recvmsg(socketFd, &msg, MSG_DONTWAIT);
        if (bytes >= 0) {
            const bool slotTruncated = (msg.msg_flags & MSG_TRUNC) != 0
                || static_cast<std::size_t>(bytes) > slots[count].buffer.size();
            if (count < outSourceAddresses.size()) {
                outSourceAddresses[count].setFromNative(&ss, msg.msg_namelen);
            }
            slots[count++].size = slotTruncated ? 0 : static_cast<std::size_t>(bytes);
            slots[count - 1].truncated = slotTruncated;
            truncated = truncated || slotTruncated;
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return batchError(errno, count);
        }
        break;
    }
    if (truncated) {
        return BatchResult::err({ count, messageTooLarge() });
    }
    return count > 0
        ? BatchResult::ok(count)
        : BatchResult::err({ 0, ReceiveTimeout { } });
#endif
}

} // namespace Common::Net
