#pragma once

#include "core/memory/ObserverPtr.hpp"
#include <concepts>
#include <type_traits>

namespace Core::Memory {

/**
 * @brief Rebindable, non-owning reference wrapper representing a non-null borrowed object.
 *
 * Provides safe borrowing semantics similar to a C++ reference `T&`, but is
 * reassignable (rebindable), container-friendly (can be stored in `std::vector`, `std::optional`),
 * disallows binding to temporary rvalues at compile time, and provides pointer-like member access (`->`).
 *
 * Can implicitly convert to `T&` and `ObserverPtr<T>`.
 *
 * @tparam T The type of object being borrowed.
 */
template <typename T>
class Borrow {
public:
    using element_type = T;

    /**
     * @brief Constructs a Borrow from an lvalue reference.
     */
    constexpr Borrow(T& ref) noexcept
        : m_ptr(&ref)
    {
    }

    /**
     * @brief Deleted rvalue constructor prevents binding to temporary objects.
     */
    Borrow(T&&) = delete;

    /**
     * @brief Copy constructor from compatible Borrow<U>.
     */
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    constexpr Borrow(const Borrow<U>& other) noexcept
        : m_ptr(&other.get())
    {
    }

    constexpr ~Borrow() noexcept = default;
    constexpr Borrow(const Borrow&) noexcept = default;
    constexpr Borrow& operator=(const Borrow&) noexcept = default;
    constexpr Borrow(Borrow&&) noexcept = default;
    constexpr Borrow& operator=(Borrow&&) noexcept = default;

    /**
     * @brief Rebinds this Borrow to another lvalue reference.
     */
    constexpr Borrow& operator=(T& ref) noexcept
    {
        m_ptr = &ref;
        return *this;
    }

    Borrow& operator=(T&&) = delete;

    /**
     * @brief Returns a reference to the borrowed object.
     */
    [[nodiscard]] constexpr T& get() const noexcept
    {
        return *m_ptr;
    }

    /**
     * @brief Dereferences the borrowed object.
     */
    [[nodiscard]] constexpr T& operator*() const noexcept
    {
        return *m_ptr;
    }

    /**
     * @brief Member access operator forwarding to the borrowed object.
     */
    [[nodiscard]] constexpr T* operator->() const noexcept
    {
        return m_ptr;
    }

    /**
     * @brief Implicit conversion to lvalue reference.
     */
    constexpr operator T&() const noexcept
    {
        return *m_ptr;
    }

    /**
     * @brief Implicit conversion to non-owning ObserverPtr<T>.
     */
    constexpr operator ObserverPtr<T>() const noexcept
    {
        return ObserverPtr<T>(m_ptr);
    }

    template <typename U>
    [[nodiscard]] constexpr bool operator==(const Borrow<U>& other) const noexcept
    {
        return m_ptr == &other.get();
    }

    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const Borrow<U>& other) const noexcept
    {
        return m_ptr <=> &other.get();
    }

    template <typename U>
    [[nodiscard]] constexpr bool operator==(const U* raw) const noexcept
    {
        return m_ptr == raw;
    }

private:
    T* m_ptr;
};

// Deduction guide
template <typename T>
Borrow(T&) -> Borrow<T>;

} // namespace Core::Memory

