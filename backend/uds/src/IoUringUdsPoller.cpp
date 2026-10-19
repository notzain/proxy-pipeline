#if defined(__linux__)

#include "IoUringUdsPoller.hpp"
#include "log/Log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>

namespace Proxy::Backend::Uds {

IoUringUdsPoller::IoUringUdsPoller()
{
    int ret = io_uring_queue_init(64, &m_ring, 0);
    if (ret < 0) {
        Proxy::Log::error("[IoUringUdsPoller] Failed to initialize io_uring: {}", std::strerror(-ret));
        m_initialized = false;
    } else {
        m_initialized = true;
    }
}

IoUringUdsPoller::~IoUringUdsPoller()
{
    if (m_initialized) {
        io_uring_queue_exit(&m_ring);
        m_initialized = false;
    }
}

std::size_t IoUringUdsPoller::receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout)
{
    if (!m_initialized || socketFd < 0 || destination.empty()) {
        return 0;
    }

    struct io_uring_sqe* sqe = io_uring_get_sqe(&m_ring);
    if (!sqe) {
        return 0;
    }

    sockaddr_un clientAddr { };
    struct iovec iov { };
    iov.iov_base = destination.data();
    iov.iov_len = destination.size();

    struct msghdr msg { };
    msg.msg_name = &clientAddr;
    msg.msg_namelen = sizeof(clientAddr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    io_uring_prep_recvmsg(sqe, socketFd, &msg, 0);
    io_uring_submit(&m_ring);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct __kernel_timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct io_uring_cqe* cqe = nullptr;
    int ret = io_uring_wait_cqe_timeout(&m_ring, &cqe, &ts);

    if (ret < 0) {
        return 0;
    }

    std::size_t bytesRead = 0;
    if (cqe) {
        if (cqe->res > 0) {
            bytesRead = static_cast<std::size_t>(cqe->res);
            Proxy::Log::debug("[IoUringUdsPoller] Received {} bytes on UDS", bytesRead);
        }
        io_uring_cqe_seen(&m_ring, cqe);
    }

    return bytesRead;
}

} // namespace Proxy::Backend::Uds

#endif // __linux__
