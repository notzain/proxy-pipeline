#pragma once

#include "common/compat/StopToken.hpp"

#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<thread>)
#include <thread>
#endif

#if defined(__cpp_lib_jthread) && (__cpp_lib_jthread >= 201911L)
#define PROXY_HAS_STD_JTHREAD 1
#endif

#if defined(PROXY_HAS_STD_JTHREAD)

namespace Common::Compat {

using jthread = std::jthread;

} // namespace Common::Compat

#else

#include "common/compat/StopToken.hpp"
#include <thread>
#include <utility>

namespace Common::Compat {

class jthread {
public:
    using id = std::thread::id;
    using native_handle_type = std::thread::native_handle_type;

    jthread() noexcept = default;

    template <typename Function, typename... Args,
        std::enable_if_t<!std::is_same_v<std::remove_cv_t<std::remove_reference_t<Function>>, jthread>, int> = 0>
    explicit jthread(Function&& f, Args&&... args)
        : m_source()
    {
        if constexpr (std::is_invocable_v<std::decay_t<Function>, stop_token, std::decay_t<Args>...>) {
            m_thread = std::thread(
                std::forward<Function>(f),
                m_source.get_token(),
                std::forward<Args>(args)...);
        } else {
            m_thread = std::thread(
                std::forward<Function>(f),
                std::forward<Args>(args)...);
        }
    }

    ~jthread()
    {
        if (joinable()) {
            request_stop();
            join();
        }
    }

    jthread(const jthread&) = delete;
    jthread& operator=(const jthread&) = delete;

    jthread(jthread&& other) noexcept
        : m_source(std::move(other.m_source))
        , m_thread(std::move(other.m_thread))
    {
    }

    jthread& operator=(jthread&& other) noexcept
    {
        if (this != &other) {
            if (joinable()) {
                request_stop();
                join();
            }
            m_source = std::move(other.m_source);
            m_thread = std::move(other.m_thread);
        }
        return *this;
    }

    void swap(jthread& other) noexcept
    {
        m_source.swap(other.m_source);
        m_thread.swap(other.m_thread);
    }

    [[nodiscard]] bool joinable() const noexcept
    {
        return m_thread.joinable();
    }

    void join()
    {
        m_thread.join();
    }

    void detach()
    {
        m_thread.detach();
    }

    [[nodiscard]] id get_id() const noexcept
    {
        return m_thread.get_id();
    }

    [[nodiscard]] native_handle_type native_handle()
    {
        return m_thread.native_handle();
    }

    [[nodiscard]] stop_source get_stop_source() noexcept
    {
        return m_source;
    }

    [[nodiscard]] stop_token get_token() const noexcept
    {
        return m_source.get_token();
    }

    bool request_stop() noexcept
    {
        return m_source.request_stop();
    }

    [[nodiscard]] static unsigned int hardware_concurrency() noexcept
    {
        return std::thread::hardware_concurrency();
    }

private:
    stop_source m_source;
    std::thread m_thread;
};

} // namespace Common::Compat

#endif
