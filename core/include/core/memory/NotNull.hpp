#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace Core::Memory {

/**
 * @brief Zero-overhead non-nullable pointer wrapper enforcing non-null invariants at compile and runtime.
 *
 * Wraps any pointer or pointer-like type (such as raw `T*`, `ObserverPtr<T>`, `std::unique_ptr<T>`).
 * Construction or assignment from `std::nullptr_t` is deleted at compile-time.
 * Runtime construction asserts that the pointer is non-null.
 *
 * @tparam Ptr The underlying pointer or pointer-like type.
 */
template <typename Ptr>
class NotNull {
public:
    static_assert(!std::is_same_v<Ptr, std::nullptr_t>, "NotNull cannot wrap std::nullptr_t");

    using element_type = typename std::pointer_traits<Ptr>::element_type;

    /**
     * @brief Constructs a NotNull from a pointer-like value, asserting it is not null.
     */
    template <typename U>
        requires std::convertible_to<U, Ptr>
    constexpr explicit NotNull(U&& ptr)
        : m_ptr(std::forward<U>(ptr))
    {
        assert(m_ptr != nullptr && "NotNull cannot be initialized with nullptr");
    }

    /**
     * @brief Deleted construction from nullptr_t to catch null assignment at compile time.
     */
    NotNull(std::nullptr_t) = delete;

    /**
     * @brief Copy constructor from compatible NotNull<U>.
     */
    template <typename U>
        requires std::convertible_to<U, Ptr>
    constexpr NotNull(const NotNull<U>& other)
        : m_ptr(other.get())
    {
    }

    /**
     * @brief Move constructor from compatible NotNull<U>.
     */
    template <typename U>
        requires std::convertible_to<U, Ptr>
    constexpr NotNull(NotNull<U>&& other)
        : m_ptr(std::move(other).get())
    {
    }

    constexpr ~NotNull() = default;
    constexpr NotNull(const NotNull&) = default;
    constexpr NotNull& operator=(const NotNull&) = default;
    constexpr NotNull(NotNull&&) noexcept = default;
    constexpr NotNull& operator=(NotNull&&) noexcept = default;

    /**
     * @brief Assignment from compatible pointer, asserting non-null.
     */
    template <typename U>
        requires std::convertible_to<U, Ptr>
    constexpr NotNull& operator=(U&& ptr)
    {
        assert(ptr != nullptr && "NotNull cannot be assigned nullptr");
        m_ptr = std::forward<U>(ptr);
        return *this;
    }

    NotNull& operator=(std::nullptr_t) = delete;

    /**
     * @brief Returns a reference to the underlying pointer.
     */
    [[nodiscard]] constexpr const Ptr& get() const noexcept
    {
        return m_ptr;
    }

    /**
     * @brief Returns a reference to the underlying pointer (mutable).
     */
    [[nodiscard]] constexpr Ptr& get() noexcept
    {
        return m_ptr;
    }

    /**
     * @brief Dereferences the underlying pointer.
     */
    [[nodiscard]] constexpr decltype(auto) operator*() const noexcept
    {
        return *m_ptr;
    }

    /**
     * @brief Member access operator forwarding to the underlying pointer.
     */
    [[nodiscard]] constexpr decltype(auto) operator->() const noexcept
    {
        return m_ptr;
    }

    /**
     * @brief Implicit conversion to the underlying pointer type.
     */
    constexpr operator const Ptr&() const noexcept
    {
        return m_ptr;
    }

    /**
     * @brief Implicit conversion to the underlying pointer type (mutable).
     */
    constexpr operator Ptr&() noexcept
    {
        return m_ptr;
    }

    template <typename U>
    [[nodiscard]] constexpr bool operator==(const NotNull<U>& other) const noexcept
    {
        return m_ptr == other.get();
    }

    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(const NotNull<U>& other) const noexcept
    {
        return m_ptr <=> other.get();
    }

    template <typename U>
    [[nodiscard]] constexpr bool operator==(const U& other) const noexcept
    {
        return m_ptr == other;
    }

    bool operator==(std::nullptr_t) const = delete;

private:
    Ptr m_ptr;
};

// Deduction guide for raw pointers
template <typename T>
NotNull(T*) -> NotNull<T*>;

// Deduction guide for general pointer types
template <typename Ptr>
NotNull(Ptr) -> NotNull<Ptr>;

} // namespace Core::Memory
