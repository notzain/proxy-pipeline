#pragma once

#include <csignal>

namespace Core::Handler {

/**
 * @brief Thread-safe manager for process-level POSIX signal handling (SIGINT / SIGTERM).
 *
 * Guarantees exclusive ownership so only one active runner or lifecycle manager installs
 * signal handlers at a time, restoring previous actions upon release.
 */
class PosixSignalManager {
public:
    /**
     * @brief Acquires process signal ownership and registers SIGINT and SIGTERM handlers.
     *
     * @param owner Pointer to the owning instance (e.g., HandlerRunner).
     * @throws std::logic_error If signal handling is already owned by another instance.
     * @throws std::system_error If sigaction registration fails.
     */
    static void acquire(const void* owner);

    /**
     * @brief Releases process signal ownership and restores previous signal handlers.
     *
     * @param owner Pointer to the owning instance. Safe to call even if not owning.
     */
    static void release(const void* owner) noexcept;

    /**
     * @brief Checks if a process shutdown signal (SIGINT or SIGTERM) was received.
     */
    [[nodiscard]] static bool isShutdownRequested() noexcept;

    /**
     * @brief Clears the shutdown request flag.
     */
    static void reset() noexcept;
};

} // namespace Core::Handler

