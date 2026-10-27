#if defined(__linux__)

#include "IoUringSocketPoller.hpp"
#include "PosixSocketPoller.hpp"

#include <cerrno>
#include <chrono>
#include <liburing.h>
#include <sys/socket.h>
#include <system_error>

namespace Backend {

struct IoUringSocketPoller::RingImpl {
    struct io_uring ring { };
    sockaddr_storage ss { };
    struct iovec iov { };
    struct msghdr msg { };
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

BytesResult IoUringSocketPoller::receive(
    int socketFd,
    Common::Compat::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (!m_impl || !m_impl->initialized || socketFd < 0 || destination.empty()) {
        if (socketFd < 0) {
            return BytesResult::failure(receiveClosed());
        }
        if (destination.empty()) {
            return BytesResult::failure(receiveFailure(std::make_error_code(std::errc::invalid_argument)));
        }
        PosixSocketPoller fallback;
        return fallback.receive(socketFd, destination, timeout, outSourceAddress);
    }

    // Fast-path: speculative non-blocking read avoids ring submission when data is ready
    {
        sockaddr_storage directSs { };
        socklen_t addrLen = sizeof(directSs);
        sockaddr* saPtr = outSourceAddress ? reinterpret_cast<sockaddr*>(&directSs) : nullptr;
        socklen_t* saLenPtr = outSourceAddress ? &addrLen : nullptr;

        ssize_t directBytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            MSG_DONTWAIT,
            saPtr,
            saLenPtr);

        if (directBytes >= 0) {
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&directSs, addrLen);
            }
            return BytesResult::success(static_cast<std::size_t>(directBytes));
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno == EBADF || errno == ENOTSOCK) {
                return BytesResult::failure(receiveClosed());
            }
            return BytesResult::failure(receiveFailure(std::error_code(errno, std::generic_category())));
        }
    }

    if (timeout.count() == 0) {
        return BytesResult::failure(receiveTimeout());
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_impl->ring);
    if (!sqe) {
        return BytesResult::failure(
            receiveFailure(std::make_error_code(std::errc::resource_unavailable_try_again)));
    }

    constexpr std::uint64_t kRecvUserData = 1;
    constexpr std::uint64_t kCancelUserData = 2;

    m_impl->ss = sockaddr_storage { };
    m_impl->iov = iovec { };
    m_impl->iov.iov_base = destination.data();
    m_impl->iov.iov_len = destination.size();

    m_impl->msg = msghdr { };
    m_impl->msg.msg_iov = &m_impl->iov;
    m_impl->msg.msg_iovlen = 1;
    if (outSourceAddress) {
        m_impl->msg.msg_name = &m_impl->ss;
        m_impl->msg.msg_namelen = sizeof(m_impl->ss);
    }

    io_uring_prep_recvmsg(sqe, socketFd, &m_impl->msg, 0);
    io_uring_sqe_set_data64(sqe, kRecvUserData);

    int ret = io_uring_submit(&m_impl->ring);
    if (ret < 0) {
        return BytesResult::failure(receiveFailure(std::error_code(-ret, std::generic_category())));
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct __kernel_timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct io_uring_cqe* cqe = nullptr;
    ret = io_uring_wait_cqe_timeout(&m_impl->ring, &cqe, &ts);

    BytesResult receiveResult = BytesResult::failure(receiveTimeout());

    if (ret < 0) {
        // Timeout or interruption: cancel the in-flight recvmsg SQE and reap all CQEs
        // before returning so the kernel never writes to m_impl->msg or destination after return.
        struct io_uring_sqe* cancelSqe = io_uring_get_sqe(&m_impl->ring);
        bool cancelSubmitted = false;
        if (cancelSqe) {
            io_uring_prep_cancel64(cancelSqe, kRecvUserData, 0);
            io_uring_sqe_set_data64(cancelSqe, kCancelUserData);
            cancelSubmitted = (io_uring_submit(&m_impl->ring) >= 0);
        }

        bool recvReaped = false;
        bool cancelReaped = !cancelSubmitted;
        while (!recvReaped || !cancelReaped) {
            struct io_uring_cqe* drainCqe = nullptr;
            if (io_uring_wait_cqe(&m_impl->ring, &drainCqe) < 0 || !drainCqe) {
                break;
            }
            const auto tag = io_uring_cqe_get_data64(drainCqe);
            if (tag == kRecvUserData) {
                if (drainCqe->res >= 0) {
                    receiveResult = BytesResult::success(static_cast<std::size_t>(drainCqe->res));
                    if (outSourceAddress) {
                        outSourceAddress->setFromNative(&m_impl->ss, m_impl->msg.msg_namelen);
                    }
                } else if (-drainCqe->res != ECANCELED && -drainCqe->res != EAGAIN) {
                    receiveResult = BytesResult::failure(
                        (-drainCqe->res == EBADF || -drainCqe->res == ENOTSOCK)
                            ? receiveClosed()
                            : receiveFailure(std::error_code(-drainCqe->res, std::generic_category())));
                }
                recvReaped = true;
            } else if (tag == kCancelUserData) {
                cancelReaped = true;
            }
            io_uring_cqe_seen(&m_impl->ring, drainCqe);
        }
        if (!receiveResult.hasValue() && !isReceiveTimeout(receiveResult.error()) && ret != -ETIME) {
            return BytesResult::failure(receiveFailure(std::error_code(-ret, std::generic_category())));
        }
        return receiveResult;
    }

    if (cqe) {
        if (cqe->res >= 0) {
            receiveResult = BytesResult::success(static_cast<std::size_t>(cqe->res));
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&m_impl->ss, m_impl->msg.msg_namelen);
            }
        } else if (-cqe->res != ECANCELED && -cqe->res != EAGAIN) {
            receiveResult = BytesResult::failure(
                (-cqe->res == EBADF || -cqe->res == ENOTSOCK)
                    ? receiveClosed()
                    : receiveFailure(std::error_code(-cqe->res, std::generic_category())));
        }
        io_uring_cqe_seen(&m_impl->ring, cqe);
    }

    return receiveResult;
}

BatchResult IoUringSocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<BufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    return ISocketPoller::receiveBatch(socketFd, slots, timeout, outSourceAddresses);
}

} // namespace Backend

#endif // __linux__
