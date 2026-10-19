#include "PosixUdsPoller.hpp"
#include "backend/uds/IUdsPoller.hpp"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include "KqueueUdsPoller.hpp"
#elif defined(__linux__)
#include "IoUringUdsPoller.hpp"
#endif

namespace Proxy::Backend::Uds {

std::unique_ptr<IUdsPoller> createUdsPoller()
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return std::make_unique<KqueueUdsPoller>();
#elif defined(__linux__)
    return std::make_unique<IoUringUdsPoller>();
#else
    return std::make_unique<PosixUdsPoller>();
#endif
}

} // namespace Proxy::Backend::Uds
