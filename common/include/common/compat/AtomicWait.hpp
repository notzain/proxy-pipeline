#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
#define PROXY_HAS_STD_ATOMIC_WAIT 1
#endif
#endif

#if defined(PROXY_HAS_STD_ATOMIC_WAIT)

namespace Common::Compat {

template <typename AtomicT, typename ValT>
inline void atomic_wait(const AtomicT& a, ValT oldVal, std::memory_order order = std::memory_order_seq_cst) noexcept
{
    a.wait(oldVal, order);
}

template <typename AtomicT>
inline void atomic_notify_one(AtomicT& a) noexcept
{
    a.notify_one();
}

template <typename AtomicT>
inline void atomic_notify_all(AtomicT& a) noexcept
{
    a.notify_all();
}

} // namespace Common::Compat

#else

/**
 * @file AtomicWait.hpp
 * @brief Zero-overhead C++17 polyfill for C++20 std::atomic::wait, notify_one, and notify_all.
 *
 * In C++20, std::atomic<T>::wait() provides hardware/kernel-assisted address waiting without
 * recurring mutex or condition_variable allocations.
 *
 * When compiling under C++17 where std::atomic::wait is unavailable, this implementation delegates
 * directly to platform-native address-wait primitives:
 *
 * - macOS / iOS (Darwin):
 *   Uses `__ulock_wait` and `__ulock_wake` (Darwin micro-lock kernel API).
 *   Reference implementation in LLVM libc++:
 *   https://github.com/llvm/llvm-project/blob/main/libcxx/src/atomic.cpp
 *   Operation: `UL_COMPARE_AND_WAIT` (1), wake flag `ULF_WAKE_ALL` (0x00000100).
 *
 * - Linux:
 *   Uses the `futex` system call via `SYS_futex` with `FUTEX_WAIT_PRIVATE` and `FUTEX_WAKE_PRIVATE`.
 *   Reference implementation in GNU libstdc++:
 *   https://gcc.gnu.org/git/?p=gcc.git;a=blob;f=libstdc%2B%2B-v3/include/bits/atomic_wait.h
 *   and Linux man-pages:
 *   https://man7.org/linux/man-pages/man2/futex.2.html
 *
 * - Windows:
 *   Uses `WaitOnAddress`, `WakeByAddressSingle`, and `WakeByAddressAll` (synchapi.h / Synchronization.lib).
 *   Reference documentation:
 *   https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress
 *
 * - Generic Fallback (other platforms):
 *   Uses a fixed-size striped table of condition variables and mutexes, with notification
 *   broadcasting to prevent lost wakeups on hash collisions (similar to libc++ global table fallback).
 */

#if defined(__APPLE__)
extern "C" {
int __ulock_wait(std::uint32_t operation, void* addr, std::uint64_t value, std::uint32_t timeout);
int __ulock_wake(std::uint32_t operation, void* addr, std::uint64_t wake_value);
}
#define PROXY_ULOCK_COMPARE_AND_WAIT 1
#define PROXY_ULOCK_WAKE_ALL 0x00000100

namespace Common::Compat {

template <typename AtomicT, typename ValT>
inline void atomic_wait(const AtomicT& a, ValT oldVal, std::memory_order order = std::memory_order_seq_cst) noexcept
{
    static_assert(sizeof(ValT) <= sizeof(std::uint64_t), "atomic_wait requires scalar value fitting uint64_t");
    void* addr = const_cast<void*>(static_cast<const void*>(&a));
    while (a.load(order) == oldVal) {
        // __ulock_wait takes a 64-bit value to compare against address contents.
        // If *addr == oldVal, the thread sleeps until woken by __ulock_wake.
        __ulock_wait(PROXY_ULOCK_COMPARE_AND_WAIT, addr, static_cast<std::uint64_t>(oldVal), 0);
    }
}

template <typename AtomicT>
inline void atomic_notify_one(AtomicT& a) noexcept
{
    void* addr = static_cast<void*>(&a);
    __ulock_wake(PROXY_ULOCK_COMPARE_AND_WAIT, addr, 0);
}

template <typename AtomicT>
inline void atomic_notify_all(AtomicT& a) noexcept
{
    void* addr = static_cast<void*>(&a);
    __ulock_wake(PROXY_ULOCK_COMPARE_AND_WAIT | PROXY_ULOCK_WAKE_ALL, addr, 0);
}

} // namespace Common::Compat

#elif defined(__linux__)

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace Common::Compat {

template <typename AtomicT, typename ValT>
inline void atomic_wait(const AtomicT& a, ValT oldVal, std::memory_order order = std::memory_order_seq_cst) noexcept
{
    static_assert(sizeof(ValT) <= sizeof(int), "Linux futex requires 32-bit scalar value");
    void* addr = const_cast<void*>(static_cast<const void*>(&a));
    while (a.load(order) == oldVal) {
        syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, static_cast<int>(oldVal), nullptr, nullptr, 0);
    }
}

template <typename AtomicT>
inline void atomic_notify_one(AtomicT& a) noexcept
{
    void* addr = static_cast<void*>(&a);
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}

template <typename AtomicT>
inline void atomic_notify_all(AtomicT& a) noexcept
{
    void* addr = static_cast<void*>(&a);
    syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, INT32_MAX, nullptr, nullptr, 0);
}

} // namespace Common::Compat

#elif defined(_WIN32)

#include <windows.h>

namespace Common::Compat {

template <typename AtomicT, typename ValT>
inline void atomic_wait(const AtomicT& a, ValT oldVal, std::memory_order order = std::memory_order_seq_cst) noexcept
{
    void* addr = const_cast<void*>(static_cast<const void*>(&a));
    while (a.load(order) == oldVal) {
        WaitOnAddress(addr, &oldVal, sizeof(ValT), INFINITE);
    }
}

template <typename AtomicT>
inline void atomic_notify_one(AtomicT& a) noexcept
{
    WakeByAddressSingle(static_cast<void*>(&a));
}

template <typename AtomicT>
inline void atomic_notify_all(AtomicT& a) noexcept
{
    WakeByAddressAll(static_cast<void*>(&a));
}

} // namespace Common::Compat

#else

// Generic striped condition variable table fallback for unknown OS targets
#include <condition_variable>
#include <mutex>

namespace Common::Compat {

namespace detail {

    struct AtomicWaitTable {
        static constexpr std::size_t TableSize = 64;
        struct Entry {
            alignas(64) std::mutex mutex;
            std::condition_variable cv;
        };
        Entry entries[TableSize];

        static Entry& get_entry(const void* addr) noexcept
        {
            static AtomicWaitTable table;
            std::uintptr_t val = reinterpret_cast<std::uintptr_t>(addr);
            std::size_t idx = (val >> 6) % TableSize;
            return table.entries[idx];
        }
    };

} // namespace detail

template <typename AtomicT, typename ValT>
inline void atomic_wait(const AtomicT& a, ValT oldVal, std::memory_order order = std::memory_order_seq_cst) noexcept
{
    if (a.load(order) != oldVal) {
        return;
    }
    auto& entry = detail::AtomicWaitTable::get_entry(&a);
    std::unique_lock<std::mutex> lock(entry.mutex);
    while (a.load(order) == oldVal) {
        entry.cv.wait(lock);
    }
}

template <typename AtomicT>
inline void atomic_notify_one(AtomicT& a) noexcept
{
    auto& entry = detail::AtomicWaitTable::get_entry(&a);
    {
        std::lock_guard<std::mutex> lock(entry.mutex);
    }
    entry.cv.notify_all();
}

template <typename AtomicT>
inline void atomic_notify_all(AtomicT& a) noexcept
{
    auto& entry = detail::AtomicWaitTable::get_entry(&a);
    {
        std::lock_guard<std::mutex> lock(entry.mutex);
    }
    entry.cv.notify_all();
}

} // namespace Common::Compat

#endif

#endif
