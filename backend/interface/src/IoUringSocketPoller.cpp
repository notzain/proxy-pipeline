#if defined(__linux__)

#include "IoUringSocketPoller.hpp"

#include <chrono>
#include <liburing.h>
#include <sys/socket.h>

namespace Backend {

struct IoUringSocketPoller::RingImpl {
    struct io_uring ring { };
    bool initialized { false };
};

IoUringSocketPoller::IoUringSocketPoller()
    : m_impl(new RingImpl())
{
    int ret = io_uring_queue_init(64, &m_impl->ring, 0);
    if (ret >= 0) {
        m_impl->initialized = true;
    }
}

IoUringSocketPoller::~IoUringSocketPoller()
{
    if (m_impl) {
        if (m_impl->initialized) {
            io_uring_queue_exit(&m_impl->ring);
        }
        delete m_impl;
        m_impl = nullptr;
    }
}

std::size_t IoUringSocketPoller::receive(
    int socketFd,
    std::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (!m_impl || !m_impl->initialized || socketFd < 0 || destination.empty()) {
        return 0;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_impl->ring);
    if (!sqe) {
        return 0;
    }

    sockaddr_storage ss { };
    struct iovec iov { };
    iov.iov_base = destination.data();
    iov.iov_len = destination.size();

    struct msghdr msg { };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (outSourceAddress) {
        msg.msg_name = &ss;
        msg.msg_namelen = sizeof(ss);
    }

    io_uring_prep_recvmsg(sqe, socketFd, &msg, 0);
    io_uring_sqe_set_data(sqe, nullptr);

    int ret = io_uring_submit(&m_impl->ring);
    if (ret < 0) {
        return 0;
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct __kernel_timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct io_uring_cqe* cqe = nullptr;
    ret = io_uring_wait_cqe_timeout(&m_impl->ring, &cqe, &ts);

    if (ret < 0) {
        return 0;
    }

    std::size_t bytesRead = 0;
    if (cqe) {
        if (cqe->res > 0) {
            bytesRead = static_cast<std::size_t>(cqe->res);
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&ss, msg.msg_namelen);
            }
        }
        io_uring_cqe_seen(&m_impl->ring, cqe);
    }

    return bytesRead;
}

std::size_t IoUringSocketPoller::receiveBatch(
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

    // Speculative non-blocking drain
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

    if (count > 0 || timeout.count() == 0) {
        return count;
    }

    // Wait for the first packet via io_uring
    SocketAddress* firstAddr = (!outSourceAddresses.empty()) ? &outSourceAddresses[0] : nullptr;
    std::size_t firstBytes = receive(socketFd, destinations[0], timeout, firstAddr);
    if (firstBytes == 0) {
        return 0;
    }
    outSizes[0] = firstBytes;
    count = 1;

    // Drain remaining ready datagrams
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

    return count;
}

} // namespace Backend

#endif // __linux__
