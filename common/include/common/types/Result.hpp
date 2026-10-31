#pragma once

#include "common/types/Variant.hpp"
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Common::Types {

/**
 * @brief Tagged wrapper representing a successful outcome.
 * @tparam T Contained value type.
 */
template <typename T>
struct Ok {
    T value;

    constexpr explicit Ok(const T& val)
        : value(val)
    {
    }

    constexpr explicit Ok(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value(std::move(val))
    {
    }

    template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<T, Args...>>>
    constexpr explicit Ok(std::in_place_t, Args&&... args)
        : value(std::forward<Args>(args)...)
    {
    }
};

/**
 * @brief Tagged wrapper representing a failed outcome.
 * @tparam E Contained error type.
 */
template <typename E>
struct Err {
    E error;

    constexpr explicit Err(const E& err)
        : error(err)
    {
    }

    constexpr explicit Err(E&& err) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error(std::move(err))
    {
    }

    template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<E, Args...>>>
    constexpr explicit Err(std::in_place_t, Args&&... args)
        : error(std::forward<Args>(args)...)
    {
    }
};

template <typename T>
Ok(T) -> Ok<T>;

template <typename E>
Err(E) -> Err<E>;

/**
 * @brief A type-safe value-or-error result with mutually exclusive alternatives,
 * modeled after Rust's Result<T, E>.
 *
 * @tparam T The success value type.
 * @tparam E The error type.
 */
template <typename T, typename E>
class Result {
public:
    using ValueType = T;
    using ErrorType = E;
    using OkType = Ok<T>;
    using ErrType = Err<E>;

    constexpr Result(OkType okVal) noexcept(std::is_nothrow_move_constructible_v<OkType>)
        : m_storage(std::move(okVal))
    {
    }

    constexpr Result(ErrType errVal) noexcept(std::is_nothrow_move_constructible_v<ErrType>)
        : m_storage(std::move(errVal))
    {
    }

    [[nodiscard]] static constexpr Result ok(T value)
    {
        return Result(OkType(std::move(value)));
    }

    [[nodiscard]] static constexpr Result err(E error)
    {
        return Result(ErrType(std::move(error)));
    }

    [[nodiscard]] constexpr bool isOk() const noexcept
    {
        return m_storage.template is<OkType>();
    }

    [[nodiscard]] constexpr bool isErr() const noexcept
    {
        return m_storage.template is<ErrType>();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return isOk();
    }

    [[nodiscard]] T& unwrap() &
    {
        if (auto opt = m_storage.template get<OkType>()) {
            return opt->get().value;
        }
        throw std::logic_error("Attempted to unwrap an Err Result");
    }

    [[nodiscard]] const T& unwrap() const&
    {
        if (auto opt = m_storage.template get<OkType>()) {
            return opt->get().value;
        }
        throw std::logic_error("Attempted to unwrap an Err Result");
    }

    [[nodiscard]] T&& unwrap() &&
    {
        if (m_storage.template is<OkType>()) {
            return std::move(std::move(m_storage).template get<OkType>()->value);
        }
        throw std::logic_error("Attempted to unwrap an Err Result");
    }

    [[nodiscard]] E& unwrapErr() &
    {
        if (auto opt = m_storage.template get<ErrType>()) {
            return opt->get().error;
        }
        throw std::logic_error("Attempted to unwrapErr an Ok Result");
    }

    [[nodiscard]] const E& unwrapErr() const&
    {
        if (auto opt = m_storage.template get<ErrType>()) {
            return opt->get().error;
        }
        throw std::logic_error("Attempted to unwrapErr an Ok Result");
    }

    [[nodiscard]] E&& unwrapErr() &&
    {
        if (m_storage.template is<ErrType>()) {
            return std::move(std::move(m_storage).template get<ErrType>()->error);
        }
        throw std::logic_error("Attempted to unwrapErr an Ok Result");
    }

    template <typename U>
    [[nodiscard]] T unwrapOr(U&& fallback) const&
    {
        if (auto opt = m_storage.template get<OkType>()) {
            return opt->get().value;
        }
        return static_cast<T>(std::forward<U>(fallback));
    }

    template <typename U>
    [[nodiscard]] T unwrapOr(U&& fallback) &&
    {
        if (m_storage.template is<OkType>()) {
            return std::move(std::move(m_storage).template get<OkType>()->value);
        }
        return static_cast<T>(std::forward<U>(fallback));
    }

    /**
     * @brief Pattern match against the Result with overloaded callables for T and E.
     */
    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) &
    {
        auto visitor = Overload { std::forward<Fs>(callables)... };
        return m_storage.match(
            [&](OkType& okVal) -> decltype(auto) {
                return visitor(okVal.value);
            },
            [&](ErrType& errVal) -> decltype(auto) {
                return visitor(errVal.error);
            });
    }

    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) const&
    {
        auto visitor = Overload { std::forward<Fs>(callables)... };
        return m_storage.match(
            [&](const OkType& okVal) -> decltype(auto) {
                return visitor(okVal.value);
            },
            [&](const ErrType& errVal) -> decltype(auto) {
                return visitor(errVal.error);
            });
    }

    template <typename... Fs>
    constexpr decltype(auto) match(Fs&&... callables) &&
    {
        auto visitor = Overload { std::forward<Fs>(callables)... };
        return std::move(m_storage).match(
            [&](OkType&& okVal) -> decltype(auto) {
                return visitor(std::move(okVal.value));
            },
            [&](ErrType&& errVal) -> decltype(auto) {
                return visitor(std::move(errVal.error));
            });
    }

    /**
     * @brief Maps a Result<T, E> to Result<U, E> by applying a function to a contained Ok value.
     */
    template <typename Fn>
    constexpr auto map(Fn&& fn) const&
    {
        using ReturnT = std::decay_t<std::invoke_result_t<Fn, const T&>>;
        using ResultU = Result<ReturnT, E>;
        if (auto opt = m_storage.template get<OkType>()) {
            return ResultU::ok(std::invoke(std::forward<Fn>(fn), opt->get().value));
        }
        return ResultU::err(m_storage.template get<ErrType>()->get().error);
    }

    template <typename Fn>
    constexpr auto map(Fn&& fn) &&
    {
        using ReturnT = std::decay_t<std::invoke_result_t<Fn, T&&>>;
        using ResultU = Result<ReturnT, E>;
        if (m_storage.template is<OkType>()) {
            return ResultU::ok(std::invoke(std::forward<Fn>(fn), std::move(std::move(m_storage).template get<OkType>()->value)));
        }
        return ResultU::err(std::move(std::move(m_storage).template get<ErrType>()->error));
    }

    /**
     * @brief Maps a Result<T, E> to Result<T, F> by applying a function to a contained Err value.
     */
    template <typename Fn>
    constexpr auto mapErr(Fn&& fn) const&
    {
        using ReturnE = std::decay_t<std::invoke_result_t<Fn, const E&>>;
        using ResultF = Result<T, ReturnE>;
        if (auto opt = m_storage.template get<ErrType>()) {
            return ResultF::err(std::invoke(std::forward<Fn>(fn), opt->get().error));
        }
        return ResultF::ok(m_storage.template get<OkType>()->get().value);
    }

    template <typename Fn>
    constexpr auto mapErr(Fn&& fn) &&
    {
        using ReturnE = std::decay_t<std::invoke_result_t<Fn, E&&>>;
        using ResultF = Result<T, ReturnE>;
        if (m_storage.template is<ErrType>()) {
            return ResultF::err(std::invoke(std::forward<Fn>(fn), std::move(std::move(m_storage).template get<ErrType>()->error)));
        }
        return ResultF::ok(std::move(std::move(m_storage).template get<OkType>()->value));
    }

    /**
     * @brief Calls fn if the result is Ok, otherwise returns the Err value of *this.
     */
    template <typename Fn>
    constexpr auto andThen(Fn&& fn) const&
    {
        using ResultU = std::decay_t<std::invoke_result_t<Fn, const T&>>;
        if (auto opt = m_storage.template get<OkType>()) {
            return std::invoke(std::forward<Fn>(fn), opt->get().value);
        }
        return ResultU::err(m_storage.template get<ErrType>()->get().error);
    }

    template <typename Fn>
    constexpr auto andThen(Fn&& fn) &&
    {
        using ResultU = std::decay_t<std::invoke_result_t<Fn, T&&>>;
        if (m_storage.template is<OkType>()) {
            return std::invoke(std::forward<Fn>(fn), std::move(std::move(m_storage).template get<OkType>()->value));
        }
        return ResultU::err(std::move(std::move(m_storage).template get<ErrType>()->error));
    }

private:
    Common::Types::Variant<OkType, ErrType> m_storage;
};

} // namespace Common::Types
