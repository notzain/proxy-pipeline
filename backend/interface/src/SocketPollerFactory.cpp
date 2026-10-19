#include "PosixSocketPoller.hpp"
#include "backend/ISocketPoller.hpp"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include "KqueueSocketPoller.hpp"
#elif defined(__linux__)
#include "IoUringSocketPoller.hpp"
#endif

namespace Backend {

std::unique_ptr<ISocketPoller> createSocketPoller()
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return std::make_unique<KqueueSocketPoller>();
#elif defined(__linux__)
    return std::make_unique<IoUringSocketPoller>();
#else
    return std::make_unique<PosixSocketPoller>();
#endif
}

} // namespace Backend
