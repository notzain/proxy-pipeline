#pragma once

#include "common/types/Variant.hpp"
#include <utility>

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
        return m_storage.template is<Value>();
    }

    [[nodiscard]] T& value() &
    {
        return m_storage.template get<Value>()->get().value;
    }

    [[nodiscard]] const T& value() const&
    {
        return m_storage.template get<Value>()->get().value;
    }

    [[nodiscard]] T&& value() &&
    {
        return std::move(std::move(m_storage).template get<Value>()->value);
    }

    [[nodiscard]] E& error() &
    {
        return m_storage.template get<Error>()->get().error;
    }

    [[nodiscard]] const E& error() const&
    {
        return m_storage.template get<Error>()->get().error;
    }

    [[nodiscard]] E&& error() &&
    {
        return std::move(std::move(m_storage).template get<Error>()->error);
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

    Common::Types::Variant<Value, Error> m_storage;
};

} // namespace Backend
