#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace Core::Memory {

/**
 * @brief Non-owning, nullable observer pointer mimicking std::experimental::observer_ptr.
 *
 * Clearly documents non-ownership semantics at API boundaries instead of raw pointers.
 * Supports implicit construction from raw pointers and nullptr for seamless brace initialization.
 *
 * @tparam T The element type being observed.
 */
template <typename T>
class ObserverPtr {
public:
    using element_type = T;

    constexpr ObserverPtr() noexcept
        : m_ptr(nullptr)
    {
    }

    constexpr ObserverPtr(std::nullptr_t) noexcept
        : m_ptr(nullptr)
    {
    }

    constexpr ObserverPtr(element_type* ptr) noexcept
        : m_ptr(ptr)
    {
    }

    template <typename U>
        requires(std::is_convertible_v<U*, element_type*>)
    constexpr ObserverPtr(ObserverPtr<U> other) noexcept
        : m_ptr(other.get())
    {
    }

    constexpr ~ObserverPtr() noexcept = default;
    constexpr ObserverPtr(const ObserverPtr&) noexcept = default;
    constexpr ObserverPtr& operator=(const ObserverPtr&) noexcept = default;
    constexpr ObserverPtr(ObserverPtr&&) noexcept = default;
    constexpr ObserverPtr& operator=(ObserverPtr&&) noexcept = default;

    [[nodiscard]] constexpr element_type* get() const noexcept
    {
        return m_ptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return m_ptr != nullptr;
    }

    [[nodiscard]] constexpr element_type& operator*() const noexcept
    {
        return *m_ptr;
    }

    [[nodiscard]] constexpr element_type* operator->() const noexcept
    {
        return m_ptr;
    }

    constexpr element_type* release() noexcept
    {
        element_type* tmp = m_ptr;
        m_ptr = nullptr;
        return tmp;
    }

    constexpr void reset(element_type* ptr = nullptr) noexcept
    {
        m_ptr = ptr;
    }

    constexpr void swap(ObserverPtr& other) noexcept
    {
        element_type* tmp = m_ptr;
        m_ptr = other.m_ptr;
        other.m_ptr = tmp;
    }

    // --- Comparison operators ---

    template <typename U>
    [[nodiscard]] constexpr bool operator==(ObserverPtr<U> other) const noexcept
    {
        return m_ptr == other.get();
    }

    [[nodiscard]] constexpr bool operator==(std::nullptr_t) const noexcept
    {
        return m_ptr == nullptr;
    }

    template <typename U>
    [[nodiscard]] constexpr bool operator==(U* other) const noexcept
    {
        return m_ptr == other;
    }

    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(ObserverPtr<U> other) const noexcept
    {
        return m_ptr <=> other.get();
    }

    [[nodiscard]] constexpr auto operator<=>(std::nullptr_t) const noexcept
    {
        return m_ptr <=> static_cast<element_type*>(nullptr);
    }

    template <typename U>
    [[nodiscard]] constexpr auto operator<=>(U* other) const noexcept
    {
        return m_ptr <=> other;
    }

private:
    element_type* m_ptr { nullptr };
};

template <typename T>
void swap(ObserverPtr<T>& lhs, ObserverPtr<T>& rhs) noexcept
{
    lhs.swap(rhs);
}

} // namespace Core::Memory

namespace std {

template <typename T>
struct hash<Core::Memory::ObserverPtr<T>> {
    std::size_t operator()(Core::Memory::ObserverPtr<T> ptr) const noexcept
    {
        return std::hash<T*>()(ptr.get());
    }
};

} // namespace std

