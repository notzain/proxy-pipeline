#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "KqueueSocketPoller.hpp"

#include <cerrno>
#include <chrono>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>

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

} // namespace

KqueueSocketPoller::KqueueSocketPoller()
{
    m_kqueueFd = ::kqueue();
}

KqueueSocketPoller::~KqueueSocketPoller()
{
    if (m_kqueueFd >= 0) {
        ::close(m_kqueueFd);
        m_kqueueFd = -1;
    }
}

bool KqueueSocketPoller::registerSocket(int socketFd)
{
    if (m_kqueueFd < 0) {
        return false;
    }
    if (socketFd == m_registeredSocketFd) {
        return true;
    }

    struct kevent change { };
    EV_SET(&change, socketFd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (::kevent(m_kqueueFd, &change, 1, nullptr, 0, nullptr) < 0) {
        return false;
    }
    m_registeredSocketFd = socketFd;
    return true;
}

BytesResult KqueueSocketPoller::receive(
    int socketFd,
    Common::Compat::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (m_kqueueFd < 0 || socketFd < 0) {
        return BytesResult::failure(receiveClosed());
    }
    if (destination.empty()) {
        return BytesResult::failure(receiveFailure(std::make_error_code(std::errc::invalid_argument)));
    }

    sockaddr_storage address { };
    socklen_t addressLength = sizeof(address);
    sockaddr* addressPtr = outSourceAddress ? reinterpret_cast<sockaddr*>(&address) : nullptr;
    socklen_t* addressLengthPtr = outSourceAddress ? &addressLength : nullptr;
    auto readDatagram = [&]() -> BytesResult {
        ssize_t bytes = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            MSG_DONTWAIT,
            addressPtr,
            addressLengthPtr);
        if (bytes >= 0) {
            if (outSourceAddress) {
                outSourceAddress->setFromNative(&address, addressLength);
            }
            return BytesResult::success(static_cast<std::size_t>(bytes));
        }
        return receiveError(errno);
    };

    auto result = readDatagram();
    if (result.hasValue() || !isReceiveTimeout(result.error()) || timeout.count() <= 0) {
        return result;
    }
    if (!registerSocket(socketFd)) {
        return BytesResult::failure(receiveFailure(std::error_code(errno, std::generic_category())));
    }

    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct timespec waitTime { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
    struct kevent events[8] { };
    int eventCount = ::kevent(m_kqueueFd, nullptr, 0, events, 8, &waitTime);
    if (eventCount < 0) {
        return receiveError(errno);
    }
    if (eventCount == 0) {
        return BytesResult::failure(receiveTimeout());
    }

    for (int i = 0; i < eventCount; ++i) {
        if (events[i].ident != static_cast<uintptr_t>(socketFd) || events[i].filter != EVFILT_READ) {
            continue;
        }
        if (events[i].flags & EV_EOF) {
            return BytesResult::failure(receiveClosed());
        }
        if (events[i].flags & EV_ERROR) {
            return receiveError(static_cast<int>(events[i].data));
        }
        return readDatagram();
    }

    return BytesResult::failure(receiveTimeout());
}

BatchResult KqueueSocketPoller::receiveBatch(
    int socketFd,
    Common::Compat::span<BufferSlot> slots,
    Timeout timeout,
    Common::Compat::span<SocketAddress> outSourceAddresses)
{
    if (socketFd < 0) {
        return BatchResult::failure({ 0, receiveClosed() });
    }
    return ISocketPoller::receiveBatch(socketFd, slots, timeout, outSourceAddresses);
}

} // namespace Backend

#endif // __APPLE__ || BSD
