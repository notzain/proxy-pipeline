#include "backend/shm/sync/ShmSignaler.hpp"
#include <ctime>
#include <sys/time.h>

namespace Proxy::Backend::Shm {

void ShmSignaler::init(ShmSyncBlock* sync) noexcept
{
    if (!sync) {
        return;
    }
#if defined(__linux__)
    sync->futexVal = 0;
#else
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&sync->mutex, &ma);
    pthread_mutexattr_destroy(&ma);

    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&sync->cond, &ca);
    pthread_condattr_destroy(&ca);

    sync->signalSeq = 0;
#endif
}

void ShmSignaler::notifyOne(ShmSyncBlock* sync) noexcept
{
    if (!sync) {
        return;
    }
#if defined(__linux__)
    __atomic_add_fetch(&sync->futexVal, 1, __ATOMIC_RELEASE);
    syscall(SYS_futex, &sync->futexVal, FUTEX_WAKE, 1, nullptr, nullptr, 0);
#else
    pthread_mutex_lock(&sync->mutex);
    sync->signalSeq++;
    pthread_cond_signal(&sync->cond);
    pthread_mutex_unlock(&sync->mutex);
#endif
}

void ShmSignaler::notifyAll(ShmSyncBlock* sync) noexcept
{
    if (!sync) {
        return;
    }
#if defined(__linux__)
    __atomic_add_fetch(&sync->futexVal, 1, __ATOMIC_RELEASE);
    syscall(SYS_futex, &sync->futexVal, FUTEX_WAKE, INT32_MAX, nullptr, nullptr, 0);
#else
    pthread_mutex_lock(&sync->mutex);
    sync->signalSeq++;
    pthread_cond_broadcast(&sync->cond);
    pthread_mutex_unlock(&sync->mutex);
#endif
}

bool ShmSignaler::wait(ShmSyncBlock* sync, Timeout timeout) noexcept
{
    if (!sync) {
        return false;
    }

#if defined(__linux__)
    uint32_t current = __atomic_load_n(&sync->futexVal, __ATOMIC_ACQUIRE);
    struct timespec ts;
    ts.tv_sec = timeout.count() / 1000;
    ts.tv_nsec = (timeout.count() % 1000) * 1000000;

    int res = syscall(SYS_futex, &sync->futexVal, FUTEX_WAIT, current, &ts, nullptr, 0);
    return res == 0;
#else
    pthread_mutex_lock(&sync->mutex);
    uint32_t expected = sync->signalSeq;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    uint64_t ns = now.tv_nsec + (static_cast<uint64_t>(timeout.count()) * 1000000ULL);
    struct timespec abstime;
    abstime.tv_sec = now.tv_sec + (ns / 1000000000ULL);
    abstime.tv_nsec = ns % 1000000000ULL;

    bool signaled = false;
    while (sync->signalSeq == expected) {
        int r = pthread_cond_timedwait(&sync->cond, &sync->mutex, &abstime);
        if (r != 0) {
            break;
        }
    }
    if (sync->signalSeq != expected) {
        signaled = true;
    }

    pthread_mutex_unlock(&sync->mutex);
    return signaled;
#endif
}

} // namespace Proxy::Backend::Shm
