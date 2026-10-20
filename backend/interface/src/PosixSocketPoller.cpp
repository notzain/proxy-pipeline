#include "PosixSocketPoller.hpp"
#include <chrono>
#include <poll.h>
#include <sys/socket.h>

namespace Backend {

std::size_t PosixSocketPoller::receive(
    int socketFd,
    std::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (socketFd < 0 || destination.empty()) {
        return 0;
    }

    sockaddr_storage ss { };
    socklen_t addrLen = sizeof(ss);
    sockaddr* saPtr = outSourceAddress ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
    socklen_t* saLenPtr = outSourceAddress ? &addrLen : nullptr;

    // Fast-path: speculative non-blocking read avoids poll() syscall when data is already available
    ssize_t directBytes = ::recvfrom(
        socketFd,
        reinterpret_cast<char*>(destination.data()),
        destination.size(),
        MSG_DONTWAIT,
        saPtr,
        saLenPtr);

    if (directBytes > 0) {
        if (outSourceAddress) {
            outSourceAddress->setFromNative(&ss, addrLen);
        }
        return static_cast<std::size_t>(directBytes);
    }

    if (timeout.count() == 0) {
        return 0;
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);

    if (ret > 0 && (pfd.revents & POLLIN)) {
        addrLen = sizeof(ss);
        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);

        if (bytesRead > 0) {
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&ss, addrLen);
            }
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

std::size_t PosixSocketPoller::receiveBatch(
    int socketFd,
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout,
    std::span<SocketAddress> outSourceAddresses)
{
    if (socketFd < 0 || destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    std::size_t count = 0;
    const std::size_t maxBatch = destinations.size();

#if defined(__linux__)
    // True vectorized ingress: recvmmsg fetches up to 32 datagrams in a single kernel transition
    constexpr std::size_t kernelBatchMax = 32;
    const std::size_t batchTarget = std::min(maxBatch, kernelBatchMax);

    struct mmsghdr msgvec[kernelBatchMax];
    struct iovec iov[kernelBatchMax];
    sockaddr_storage addrs[kernelBatchMax];
    std::memset(msgvec, 0, sizeof(msgvec[0]) * batchTarget);

    for (std::size_t i = 0; i < batchTarget; ++i) {
        iov[i].iov_base = destinations[i].data();
        iov[i].iov_len = destinations[i].size();
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
            outSizes[i] = msgvec[i].msg_len;
            if (i < outSourceAddresses.size()) {
                outSourceAddresses[i].setFromNative(&addrs[i], msgvec[i].msg_hdr.msg_namelen);
            }
        }
        return static_cast<std::size_t>(received);
    }
#else
    // Fast-path: speculative non-blocking drain
    while (count < maxBatch) {
        sockaddr_storage ss { };
        socklen_t addrLen = sizeof(ss);
        sockaddr* saPtr = (count < outSourceAddresses.size()) ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
        socklen_t* saLenPtr = (count < outSourceAddresses.size()) ? &addrLen : nullptr;

        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destinations[count].data()),
            destinations[count].size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);

        if (bytes > 0) {
            if (count < outSourceAddresses.size()) {
                outSourceAddresses[count].setFromNative(&ss, addrLen);
            }
            outSizes[count++] = static_cast<std::size_t>(bytes);
        } else {
            break;
        }
    }
#endif

    if (count > 0 || timeout.count() == 0) {
        return count;
    }

    pollfd pfd { };
    pfd.fd = socketFd;
    pfd.events = POLLIN;

    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);

    if (ret > 0 && (pfd.revents & POLLIN)) {
        while (count < maxBatch) {
            sockaddr_storage ss { };
            socklen_t addrLen = sizeof(ss);
            sockaddr* saPtr = (count < outSourceAddresses.size()) ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
            socklen_t* saLenPtr = (count < outSourceAddresses.size()) ? &addrLen : nullptr;

            ssize_t bytes = ::recvfrom(
                socketFd,
                reinterpret_cast<char*>(destinations[count].data()),
                destinations[count].size(),
                MSG_DONTWAIT,
                saPtr,
                saLenPtr);

            if (bytes > 0) {
                if (count < outSourceAddresses.size()) {
                    outSourceAddresses[count].setFromNative(&ss, addrLen);
                }
                outSizes[count++] = static_cast<std::size_t>(bytes);
            } else {
                break;
            }
        }
    }

    return count;
}

} // namespace Backend
