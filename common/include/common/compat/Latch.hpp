#pragma once

#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<latch>)
#include <latch>
#endif

#if defined(__cpp_lib_latch) && (__cpp_lib_latch >= 201907L)
#define PROXY_HAS_STD_LATCH 1
#endif

#if defined(PROXY_HAS_STD_LATCH)

namespace Common::Compat {
using latch = std::latch;
} // namespace Common::Compat

#else

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace Common::Compat {

class latch {
public:
    explicit latch(std::ptrdiff_t expected) noexcept
        : m_counter(expected)
    {
    }

    ~latch() = default;

    latch(const latch&) = delete;
    latch& operator=(const latch&) = delete;

    void count_down(std::ptrdiff_t update = 1)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_counter -= update;
        if (m_counter <= 0) {
            m_cv.notify_all();
        }
    }

    [[nodiscard]] bool try_wait() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_counter <= 0;
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_counter <= 0; });
    }

    void arrive_and_wait(std::ptrdiff_t update = 1)
    {
        count_down(update);
        wait();
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::ptrdiff_t m_counter;
};

} // namespace Common::Compat

#endif
