#include "PosixSocketPoller.hpp"
#include "common/net/ISocketPoller.hpp"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include "KqueueSocketPoller.hpp"
#elif defined(__linux__) && defined(PROXY_HAS_LIBURING)
#include "IoUringSocketPoller.hpp"
#endif

namespace Common::Net {

std::unique_ptr<ISocketPoller> createSocketPoller()
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    return std::make_unique<KqueueSocketPoller>();
#elif defined(__linux__) && defined(PROXY_HAS_LIBURING)
    return std::make_unique<IoUringSocketPoller>();
#else
    return std::make_unique<PosixSocketPoller>();
#endif
}

} // namespace Common::Net
