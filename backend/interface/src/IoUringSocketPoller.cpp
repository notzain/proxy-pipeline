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

std::size_t IoUringSocketPoller::receive(int socketFd, std::span<std::byte> destination, Timeout timeout)
{
    if (!m_impl || !m_impl->initialized || socketFd < 0 || destination.empty()) {
        return 0;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_impl->ring);
    if (!sqe) {
        return 0;
    }

    struct iovec iov { };
    iov.iov_base = destination.data();
    iov.iov_len = destination.size();

    struct msghdr msg { };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

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
        }
        io_uring_cqe_seen(&m_impl->ring, cqe);
    }

    return bytesRead;
}

} // namespace Backend

#endif // __linux__
