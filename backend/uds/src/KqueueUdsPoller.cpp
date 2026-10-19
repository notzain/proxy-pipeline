#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "KqueueUdsPoller.hpp"
#include "log/Log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace Proxy::Backend::Uds {

KqueueUdsPoller::KqueueUdsPoller()
{
    m_kqueueFd = ::kqueue();
    if (m_kqueueFd < 0) {
        Proxy::Log::error("[KqueueUdsPoller] Failed to create kqueue: {}", std::strerror(errno));
    }
}

KqueueUdsPoller::~KqueueUdsPoller()
{
    if (m_kqueueFd >= 0) {
        ::close(m_kqueueFd);
        m_kqueueFd = -1;
    }
}

void KqueueUdsPoller::registerSocket(int socketFd)
{
    if (m_kqueueFd < 0 || socketFd == m_registeredSocketFd) {
        return;
    }

    struct kevent change { };
    EV_SET(&change, socketFd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);

    if (::kevent(m_kqueueFd, &change, 1, nullptr, 0, nullptr) < 0) {
        Proxy::Log::error("[KqueueUdsPoller] Failed to register socket fd {} in kqueue: {}", socketFd, std::strerror(errno));
    } else {
        m_registeredSocketFd = socketFd;
    }
}

std::size_t KqueueUdsPoller::receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout)
{
    if (m_kqueueFd < 0 || socketFd < 0 || destination.empty()) {
        return 0;
    }

    registerSocket(socketFd);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    struct timespec ts { };
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;

    struct kevent event { };
    int nEvents = ::kevent(m_kqueueFd, nullptr, 0, &event, 1, &ts);

    if (nEvents > 0 && event.filter == EVFILT_READ) {
        sockaddr_un clientAddr { };
        socklen_t clientLen = sizeof(clientAddr);

        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen);

        if (bytesRead > 0) {
            Proxy::Log::debug("[KqueueUdsPoller] Received {} bytes on UDS", bytesRead);
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Uds

#endif // __APPLE__ || BSD
