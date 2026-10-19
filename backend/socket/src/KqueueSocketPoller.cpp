#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "KqueueSocketPoller.hpp"
#include "log/Log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace Proxy::Backend::Socket {

KqueueSocketPoller::KqueueSocketPoller()
{
    m_kqueueFd = ::kqueue();
    if (m_kqueueFd < 0) {
        Proxy::Log::error("[KqueueSocketPoller] Failed to create kqueue: {}", std::strerror(errno));
    }
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

    if (::kevent(m_kqueueFd, &change, 1, nullptr, 0, nullptr) < 0) {
        Proxy::Log::error("[KqueueSocketPoller] Failed to register socket fd {} in kqueue: {}", socketFd, std::strerror(errno));
    } else {
        m_registeredSocketFd = socketFd;
    }
}

std::size_t KqueueSocketPoller::receive(int socketFd, std::span<std::byte> destination, Timeout timeout)
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
        sockaddr_in clientAddr { };
        socklen_t clientLen = sizeof(clientAddr);

        ssize_t bytesRead = ::recvfrom(
            socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen);

        if (bytesRead > 0) {
            char clientIp[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
            Proxy::Log::debug("[KqueueSocketPoller] Received {} bytes from {}:{}", bytesRead, clientIp, ntohs(clientAddr.sin_port));
            return static_cast<std::size_t>(bytesRead);
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Socket

#endif // __APPLE__ || BSD
