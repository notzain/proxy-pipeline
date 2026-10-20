#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "KqueueSocketPoller.hpp"

#include <chrono>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace Backend {

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

void KqueueSocketPoller::registerSocket(int socketFd)
{
    if (m_kqueueFd < 0 || socketFd == m_registeredSocketFd) {
        return;
    }

    struct kevent change { };
    EV_SET(&change, socketFd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);

    if (::kevent(m_kqueueFd, &change, 1, nullptr, 0, nullptr) >= 0) {
        m_registeredSocketFd = socketFd;
    }
}

std::size_t KqueueSocketPoller::receive(
    int socketFd,
    std::span<std::byte> destination,
    Timeout timeout,
    SocketAddress* outSourceAddress)
{
    if (m_kqueueFd < 0 || socketFd < 0 || destination.empty()) {
        return 0;
    }

    sockaddr_storage ss { };
    socklen_t addrLen = sizeof(ss);
    sockaddr* saPtr = outSourceAddress ? reinterpret_cast<sockaddr*>(&ss) : nullptr;
    socklen_t* saLenPtr = outSourceAddress ? &addrLen : nullptr;

    // Fast-path: speculative non-blocking read avoids kevent kernel roundtrip when socket buffer has data
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

    registerSocket(socketFd);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct kevent events[8] { };
    int nEvents = ::kevent(m_kqueueFd, nullptr, 0, events, 8, &ts);

    for (int i = 0; i < nEvents; ++i) {
        if (events[i].ident == static_cast<uintptr_t>(socketFd) && events[i].filter == EVFILT_READ) {
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
    }

    return 0;
}

std::size_t KqueueSocketPoller::receiveBatch(
    int socketFd,
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout,
    std::span<SocketAddress> outSourceAddresses)
{
    if (m_kqueueFd < 0 || socketFd < 0 || destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    std::size_t count = 0;
    const std::size_t maxBatch = destinations.size();

    // Speculatively drain ready datagrams non-blocking without kevent syscall
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

    // Wait via kevent for readability
    registerSocket(socketFd);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct kevent events[8] { };
    int nEvents = ::kevent(m_kqueueFd, nullptr, 0, events, 8, &ts);

    if (nEvents <= 0) {
        return 0;
    }

    // Drain ready datagrams now that socket is readable
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

#endif // __APPLE__ || BSD
