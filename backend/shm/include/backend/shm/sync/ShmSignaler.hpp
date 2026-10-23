#pragma once

#include <chrono>
#include <cstdint>
#include <pthread.h>

#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Proxy::Backend::Shm {

/**
 * @brief Cross-process signaling control block residing in shared memory.
 *
 * Provides futex wait/wake on Linux (`SYS_futex`) and process-shared pthread
 * condition variables on macOS/POSIX (`pthread_mutexattr_setpshared`).
 */
struct alignas(64) ShmSyncBlock {
#if defined(__linux__)
    alignas(64) uint32_t futexVal { 0 };
    uint8_t padding[60] { 0 };
#else
    alignas(64) pthread_mutex_t mutex;
    alignas(64) pthread_cond_t cond;
    alignas(64) uint32_t signalSeq { 0 };
    uint8_t padding[60] { 0 };
#endif
};

/**
 * @brief Cross-process kernel bypass notifier and event waiter.
 */
class ShmSignaler {
public:
    using Timeout = std::chrono::milliseconds;

    /**
     * @brief Initializes the process-shared sync block in newly created shared memory.
     *
     * @param sync Pointer to the shared memory sync block.
     */
    static void init(ShmSyncBlock* sync) noexcept;

    /**
     * @brief Signals waiting consumers or producers across process boundaries.
     *
     * @param sync Pointer to the shared memory sync block.
     */
    static void notifyOne(ShmSyncBlock* sync) noexcept;

    /**
     * @brief Signals all waiting consumers or producers across process boundaries.
     *
     * @param sync Pointer to the shared memory sync block.
     */
    static void notifyAll(ShmSyncBlock* sync) noexcept;

    /**
     * @brief Waits for a notification signal or until timeout expires.
     *
     * @param sync Pointer to the shared memory sync block.
     * @param timeout Maximum wait duration.
     * @return true if signaled; false if timed out.
     */
    static bool wait(ShmSyncBlock* sync, Timeout timeout) noexcept;
};

} // namespace Proxy::Backend::Shm
