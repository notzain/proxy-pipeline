#pragma once

#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<stop_token>)
#include <stop_token>
#endif

#if defined(__cpp_lib_jthread) || defined(__cpp_lib_stop_token) || (defined(__cplusplus) && __cplusplus >= 202002L && __has_include(<stop_token>))
#define PROXY_HAS_STD_STOP_TOKEN 1
#endif

#if defined(PROXY_HAS_STD_STOP_TOKEN)

namespace Common::Compat {

using stop_token = std::stop_token;
using stop_source = std::stop_source;
using nostopstate_t = std::nostopstate_t;
inline constexpr nostopstate_t nostopstate = std::nostopstate;

template <typename Callback>
using stop_callback = std::stop_callback<Callback>;

} // namespace Common::Compat

#else

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace Common::Compat {

class stop_source;
class stop_token;

namespace detail {

    struct StopCallbackBase {
        StopCallbackBase* next { nullptr };
        StopCallbackBase* prev { nullptr };
        virtual void execute() noexcept = 0;
        virtual ~StopCallbackBase() = default;
    };

    class StopState {
    public:
        StopState() noexcept
            : m_stopRequested(false)
            , m_stopSources(1)
            , m_stopTokens(0)
        {
        }

        bool request_stop() noexcept
        {
            StopCallbackBase* callbacksToRun = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopRequested) {
                    return false;
                }
                m_stopRequested = true;
                callbacksToRun = m_head;
                m_head = nullptr;
            }

            while (callbacksToRun) {
                StopCallbackBase* next = callbacksToRun->next;
                callbacksToRun->execute();
                callbacksToRun = next;
            }
            return true;
        }

        [[nodiscard]] bool stop_requested() const noexcept
        {
            return m_stopRequested.load(std::memory_order_acquire);
        }

        void add_source() noexcept
        {
            m_stopSources.fetch_add(1, std::memory_order_relaxed);
        }

        void release_source() noexcept
        {
            if (m_stopSources.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                check_delete();
            }
        }

        void add_token() noexcept
        {
            m_stopTokens.fetch_add(1, std::memory_order_relaxed);
        }

        void release_token() noexcept
        {
            if (m_stopTokens.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                check_delete();
            }
        }

        bool register_callback(StopCallbackBase* cb) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopRequested) {
                cb->execute();
                return false;
            }
            cb->next = m_head;
            if (m_head) {
                m_head->prev = cb;
            }
            m_head = cb;
            return true;
        }

        void remove_callback(StopCallbackBase* cb) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (cb->prev) {
                cb->prev->next = cb->next;
            } else if (m_head == cb) {
                m_head = cb->next;
            }
            if (cb->next) {
                cb->next->prev = cb->prev;
            }
        }

    private:
        void check_delete() noexcept
        {
            if (m_stopSources.load(std::memory_order_acquire) == 0 && m_stopTokens.load(std::memory_order_acquire) == 0) {
                delete this;
            }
        }

        std::atomic<bool> m_stopRequested;
        std::atomic<std::size_t> m_stopSources;
        std::atomic<std::size_t> m_stopTokens;
        std::mutex m_mutex;
        StopCallbackBase* m_head { nullptr };
    };

} // namespace detail

struct nostopstate_t {
    explicit constexpr nostopstate_t() = default;
};
inline constexpr nostopstate_t nostopstate { };

class stop_token {
public:
    stop_token() noexcept = default;

    stop_token(const stop_token& other) noexcept
        : m_state(other.m_state)
    {
        if (m_state) {
            m_state->add_token();
        }
    }

    stop_token(stop_token&& other) noexcept
        : m_state(other.m_state)
    {
        other.m_state = nullptr;
    }

    stop_token& operator=(const stop_token& other) noexcept
    {
        if (this != &other) {
            if (m_state) {
                m_state->release_token();
            }
            m_state = other.m_state;
            if (m_state) {
                m_state->add_token();
            }
        }
        return *this;
    }

    stop_token& operator=(stop_token&& other) noexcept
    {
        if (this != &other) {
            if (m_state) {
                m_state->release_token();
            }
            m_state = other.m_state;
            other.m_state = nullptr;
        }
        return *this;
    }

    ~stop_token()
    {
        if (m_state) {
            m_state->release_token();
        }
    }

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return m_state != nullptr && m_state->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept
    {
        return m_state != nullptr;
    }

    void swap(stop_token& other) noexcept
    {
        std::swap(m_state, other.m_state);
    }

    [[nodiscard]] bool operator==(const stop_token& other) const noexcept
    {
        return m_state == other.m_state;
    }

    [[nodiscard]] bool operator!=(const stop_token& other) const noexcept
    {
        return !(*this == other);
    }

private:
    friend class stop_source;
    template <typename Callback>
    friend class stop_callback;

    explicit stop_token(detail::StopState* state) noexcept
        : m_state(state)
    {
        if (m_state) {
            m_state->add_token();
        }
    }

    detail::StopState* m_state { nullptr };
};

class stop_source {
public:
    stop_source()
        : m_state(new detail::StopState())
    {
    }

    explicit stop_source(nostopstate_t) noexcept
        : m_state(nullptr)
    {
    }

    stop_source(const stop_source& other) noexcept
        : m_state(other.m_state)
    {
        if (m_state) {
            m_state->add_source();
        }
    }

    stop_source(stop_source&& other) noexcept
        : m_state(other.m_state)
    {
        other.m_state = nullptr;
    }

    stop_source& operator=(const stop_source& other) noexcept
    {
        if (this != &other) {
            if (m_state) {
                m_state->release_source();
            }
            m_state = other.m_state;
            if (m_state) {
                m_state->add_source();
            }
        }
        return *this;
    }

    stop_source& operator=(stop_source&& other) noexcept
    {
        if (this != &other) {
            if (m_state) {
                m_state->release_source();
            }
            m_state = other.m_state;
            other.m_state = nullptr;
        }
        return *this;
    }

    ~stop_source()
    {
        if (m_state) {
            m_state->release_source();
        }
    }

    [[nodiscard]] bool request_stop() noexcept
    {
        return m_state && m_state->request_stop();
    }

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return m_state != nullptr && m_state->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept
    {
        return m_state != nullptr;
    }

    [[nodiscard]] stop_token get_token() const noexcept
    {
        return stop_token(m_state);
    }

    void swap(stop_source& other) noexcept
    {
        std::swap(m_state, other.m_state);
    }

    [[nodiscard]] bool operator==(const stop_source& other) const noexcept
    {
        return m_state == other.m_state;
    }

    [[nodiscard]] bool operator!=(const stop_source& other) const noexcept
    {
        return !(*this == other);
    }

private:
    detail::StopState* m_state { nullptr };
};

template <typename Callback>
class stop_callback : private detail::StopCallbackBase {
public:
    using callback_type = Callback;

    template <typename C,
        std::enable_if_t<std::is_constructible_v<Callback, C>, int> = 0>
    explicit stop_callback(const stop_token& st, C&& cb)
        : m_callback(std::forward<C>(cb))
        , m_state(st.m_state)
    {
        if (m_state) {
            m_state->add_token();
            if (!m_state->register_callback(this)) {
                m_state->release_token();
                m_state = nullptr;
            }
        }
    }

    ~stop_callback() override
    {
        if (m_state) {
            m_state->remove_callback(this);
            m_state->release_token();
        }
    }

    stop_callback(const stop_callback&) = delete;
    stop_callback(stop_callback&&) = delete;
    stop_callback& operator=(const stop_callback&) = delete;
    stop_callback& operator=(stop_callback&&) = delete;

private:
    void execute() noexcept override
    {
        m_callback();
    }

    Callback m_callback;
    detail::StopState* m_state { nullptr };
};

} // namespace Common::Compat

#endif
