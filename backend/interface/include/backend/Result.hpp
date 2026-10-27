#pragma once

#include <utility>
#include <variant>

namespace Backend {

/**
 * @brief A value-or-error result with mutually exclusive alternatives.
 * @tparam T The success value type.
 * @tparam E The error type.
 */
template <typename T, typename E>
class Result {
    struct Value {
        T value;
    };

    struct Error {
        E error;
    };

public:
    [[nodiscard]] static Result success(T value)
    {
        return Result(Value { std::move(value) });
    }

    [[nodiscard]] static Result failure(E error)
    {
        return Result(Error { std::move(error) });
    }

    [[nodiscard]] bool hasValue() const noexcept
    {
        return std::holds_alternative<Value>(m_storage);
    }

    [[nodiscard]] T& value() &
    {
        return std::get<Value>(m_storage).value;
    }

    [[nodiscard]] const T& value() const&
    {
        return std::get<Value>(m_storage).value;
    }

    [[nodiscard]] T&& value() &&
    {
        return std::move(std::get<Value>(m_storage).value);
    }

    [[nodiscard]] E& error() &
    {
        return std::get<Error>(m_storage).error;
    }

    [[nodiscard]] const E& error() const&
    {
        return std::get<Error>(m_storage).error;
    }

    [[nodiscard]] E&& error() &&
    {
        return std::move(std::get<Error>(m_storage).error);
    }

private:
    explicit Result(Value value)
        : m_storage(std::move(value))
    {
    }

    explicit Result(Error error)
        : m_storage(std::move(error))
    {
    }

    std::variant<Value, Error> m_storage;
};

} // namespace Backend
