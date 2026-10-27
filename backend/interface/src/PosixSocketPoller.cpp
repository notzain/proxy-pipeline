#include "PosixSocketPoller.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>

namespace Backend {

namespace {

    BytesResult receiveError(int error)
    {
        if (error == EBADF || error == ENOTSOCK) {
            return BytesResult::failure(receiveClosed());
        }
        if (error == EAGAIN || error == EWOULDBLOCK) {
            return BytesResult::failure(receiveTimeout());
        }
        return BytesResult::failure(receiveFailure(std::error_code(error, std::generic_category())));
    }

    BatchResult batchError(int error, std::size_t receivedCount = 0)
    {
        ReceiveError cause;
        if (error == EBADF || error == ENOTSOCK) {
            cause = receiveClosed();
        } else if (error == EAGAIN || error == EWOULDBLOCK) {
            cause = receiveTimeout();
        } else {
            cause = receiveFailure(std::error_code(error, std::generic_category()));
        }
        return BatchResult::failure({ receivedCount, std::move(cause) });
    }

} // namespace

BytesResult PosixSocketPoller::receive(
    int socketFd,
    std::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (socketFd < 0) {
        return BytesResult::failure(receiveClosed());
    }
    if (destination.empty()) {
        return BytesResult::failure(receiveFailure(std::make_error_code(std::errc::invalid_argument)));
    }

    sockaddr_storage ss { };
    socklen_t addrLen = sizeof(ss);
    sockaddr* saPtr = outSourceAddress ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
    socklen_t* saLenPtr = outSourceAddress ? &addrLen : nullptr;
    auto readDatagram = [&]() -> BytesResult {
        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);
        if (bytes >= 0) {
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&ss, addrLen);
            }
            return BytesResult::success(static_cast<std::size_t>(bytes));
        }
        return receiveError(errno);
    };

    auto directResult = readDatagram();
    if (directResult.hasValue() || !isReceiveTimeout(directResult.error())) {
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
        return BytesResult::failure(receiveTimeout());
    }
    if (ret < 0) {
        return receiveError(errno);
    }
    if (pfd.revents & POLLNVAL) {
        return BytesResult::failure(receiveClosed());
    }
    if (pfd.revents & POLLHUP) {
        return BytesResult::failure(receiveClosed());
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
        return BytesResult::failure(receiveFailure(std::make_error_code(std::errc::io_error)));
    }
    if (!(pfd.revents & POLLIN)) {
        return BytesResult::failure(receiveFailure(std::make_error_code(std::errc::io_error)));
    }

    return readDatagram();
}

BatchResult PosixSocketPoller::receiveBatch(
    int socketFd,
    std::span<BufferSlot> slots,
    Timeout timeout,
    std::span<SocketAddress> outSourceAddresses)
{
    if (socketFd < 0) {
        return BatchResult::failure({ 0, receiveClosed() });
    }
    if (slots.empty()) {
        return BatchResult::failure({ 0, receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
    }

    std::size_t count = 0;
    const std::size_t maxBatch = slots.size();

#if defined(__linux__)
    constexpr std::size_t kernelBatchMax = 32;
    const std::size_t batchTarget = std::min(maxBatch, kernelBatchMax);
    struct mmsghdr msgvec[kernelBatchMax] { };
    struct iovec iov[kernelBatchMax] { };
    sockaddr_storage addrs[kernelBatchMax] { };

    for (std::size_t i = 0; i < batchTarget; ++i) {
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
        for (int i = 0; i < received; ++i) {
            slots[i].size = msgvec[i].msg_len;
            if (static_cast<std::size_t>(i) < outSourceAddresses.size()) {
                outSourceAddresses[i].setFromNative(&addrs[i], msgvec[i].msg_hdr.msg_namelen);
            }
        }
        return BatchResult::success(static_cast<std::size_t>(received));
    }
    if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return batchError(errno);
    }
#else
    while (count < maxBatch) {
        sockaddr_storage ss { };
        socklen_t addrLen = sizeof(ss);
        sockaddr* saPtr = (count < outSourceAddresses.size()) ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
        socklen_t* saLenPtr = (count < outSourceAddresses.size()) ? &addrLen : nullptr;
        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(slots[count].buffer.data()),
            slots[count].buffer.size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);
        if (bytes >= 0) {
            if (count < outSourceAddresses.size()) {
                outSourceAddresses[count].setFromNative(&ss, addrLen);
            }
            slots[count++].size = static_cast<std::size_t>(bytes);
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return batchError(errno, count);
        }
        break;
    }
#endif

    if (count > 0 || timeout.count() <= 0) {
        return count > 0
            ? BatchResult::success(count)
            : BatchResult::failure({ 0, receiveTimeout() });
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;
    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret == 0) {
        return BatchResult::failure({ 0, receiveTimeout() });
    }
    if (ret < 0) {
        return batchError(errno);
    }
    if (pfd.revents & POLLNVAL || pfd.revents & POLLHUP) {
        return BatchResult::failure({ 0, receiveClosed() });
    }
    if (pfd.revents & POLLERR) {
        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) < 0) {
            return batchError(errno);
        }
        return socketError != 0
            ? batchError(socketError)
            : BatchResult::failure({ 0, receiveFailure(std::make_error_code(std::errc::io_error)) });
    }
    if (!(pfd.revents & POLLIN)) {
        return BatchResult::failure({ 0, receiveFailure(std::make_error_code(std::errc::io_error)) });
    }

#if defined(__linux__)
    received = ::recvmmsg(socketFd, msgvec, static_cast<unsigned int>(batchTarget), MSG_DONTWAIT, nullptr);
    if (received < 0) {
        return batchError(errno);
    }
    for (int i = 0; i < received; ++i) {
        slots[i].size = msgvec[i].msg_len;
        if (static_cast<std::size_t>(i) < outSourceAddresses.size()) {
            outSourceAddresses[i].setFromNative(&addrs[i], msgvec[i].msg_hdr.msg_namelen);
        }
    }
    return received > 0
        ? BatchResult::success(static_cast<std::size_t>(received))
        : BatchResult::failure({ 0, receiveTimeout() });
#else
    while (count < maxBatch) {
        sockaddr_storage ss { };
        socklen_t addrLen = sizeof(ss);
        sockaddr* saPtr = (count < outSourceAddresses.size()) ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
        socklen_t* saLenPtr = (count < outSourceAddresses.size()) ? &addrLen : nullptr;
        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(slots[count].buffer.data()),
            slots[count].buffer.size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);
        if (bytes >= 0) {
            if (count < outSourceAddresses.size()) {
                outSourceAddresses[count].setFromNative(&ss, addrLen);
            }
            slots[count++].size = static_cast<std::size_t>(bytes);
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return batchError(errno, count);
        }
        break;
    }
    return count > 0
        ? BatchResult::success(count)
        : BatchResult::failure({ 0, receiveTimeout() });
#endif
}

} // namespace Backend
