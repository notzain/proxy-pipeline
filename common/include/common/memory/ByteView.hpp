#pragma once

#include "common/memory/View.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Common::Memory {

class MutableByteView;

namespace detail {

    template <typename T, typename = void>
    struct has_byte_data_and_size : std::false_type { };

    template <typename T>
    struct has_byte_data_and_size<T, std::void_t<decltype(std::declval<const T&>().data()), decltype(std::declval<const T&>().size())>> : std::true_type { };

    template <typename T>
    using byte_container_element_t = std::remove_pointer_t<decltype(std::declval<const T&>().data())>;

} // namespace detail

/**
 * @brief High-performance, non-owning view of contiguous immutable bytes.
 *
 * Inherits from View<const std::byte> (and transitively Common::Compat::span<const std::byte>)
 * while providing ergonomic byte conversions from arbitrary contiguous containers, string literals,
 * single POD structs, and zero-cost string_view / POD interpretation and comparison methods.
 */
class ByteView : public View<const std::byte> {
public:
    using Base = View<const std::byte>;
    using Base::Base;

    constexpr ByteView() noexcept = default;

    /**
     * @brief Constructs a ByteView from a pointer and element count.
     *
     * @tparam T Element type pointed to.
     * @param ptr Pointer to the first element.
     * @param count Number of elements of type T.
     */
    template <typename T>
    constexpr ByteView(const T* ptr, std::size_t count) noexcept
        : Base(reinterpret_cast<const std::byte*>(ptr), count * sizeof(T))
    {
    }

    /**
     * @brief Constructs a ByteView from any contiguous container exposing .data() and .size().
     *
     * Supports std::string, std::string_view, std::vector<T>, std::array<T, N>,
     * Common::Compat::span<T>, and custom buffer types.
     *
     * @tparam Container Contiguous container type.
     */
    template <typename Container,
        std::enable_if_t<
            detail::has_byte_data_and_size<Container>::value
                && !std::is_base_of_v<Base, std::decay_t<Container>>,
            int>
        = 0>
    constexpr ByteView(const Container& container) noexcept
        : Base(
              reinterpret_cast<const std::byte*>(container.data()),
              container.size() * sizeof(detail::byte_container_element_t<Container>))
    {
    }

    /**
     * @brief Constructs a ByteView from a string literal (excluding null terminator).
     */
    template <std::size_t N>
    constexpr ByteView(const char (&arr)[N]) noexcept
        : Base(reinterpret_cast<const std::byte*>(arr), N > 0 && arr[N - 1] == '\0' ? N - 1 : N)
    {
    }

    /**
     * @brief Constructs a ByteView from a fixed-size non-char C array.
     *
     * @tparam T Array element type.
     * @tparam N Number of elements.
     */
    template <typename T, std::size_t N,
        std::enable_if_t<!std::is_same_v<T, char>, int> = 0>
    constexpr ByteView(const T (&arr)[N]) noexcept
        : Base(reinterpret_cast<const std::byte*>(arr), N * sizeof(T))
    {
    }

    /**
     * @brief Constructs a ByteView from a single trivially copyable object (struct or scalar).
     *
     * Enables direct view of headers or POD structs: ByteView(header) without manual & or sizeof.
     *
     * @tparam T Trivially copyable struct or scalar type.
     */
    template <typename T,
        std::enable_if_t<
            std::is_trivially_copyable_v<T>
                && !detail::has_byte_data_and_size<T>::value
                && !std::is_pointer_v<T>
                && !std::is_array_v<T>
                && !std::is_base_of_v<Base, std::decay_t<T>>,
            int>
        = 0>
    constexpr explicit ByteView(const T& object) noexcept
        : Base(reinterpret_cast<const std::byte*>(std::addressof(object)), sizeof(T))
    {
    }

    /**
     * @brief Reinterprets the byte span as a string view.
     */
    [[nodiscard]] std::string_view asStringView() const noexcept
    {
        return { reinterpret_cast<const char*>(data()), size() };
    }

    /**
     * @brief Reinterprets the underlying pointer as a const char*.
     */
    [[nodiscard]] const char* asChar() const noexcept
    {
        return reinterpret_cast<const char*>(data());
    }

    /**
     * @brief Reinterprets the byte buffer as a typed const pointer.
     *
     * @tparam T Target POD or structure type.
     * @return const T* Pointer to the reinterpreted structure, or nullptr if smaller than sizeof(T).
     */
    template <typename T>
    [[nodiscard]] const T* as() const noexcept
    {
        if (size() < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(data());
    }

    /**
     * @brief Checks if this byte view starts with the given prefix.
     */
    [[nodiscard]] bool startsWith(ByteView prefix) const noexcept
    {
        if (size() < prefix.size()) {
            return false;
        }
        return std::memcmp(data(), prefix.data(), prefix.size()) == 0;
    }

    /**
     * @brief Checks if this byte view ends with the given suffix.
     */
    [[nodiscard]] bool endsWith(ByteView suffix) const noexcept
    {
        if (size() < suffix.size()) {
            return false;
        }
        return std::memcmp(data() + (size() - suffix.size()), suffix.data(), suffix.size()) == 0;
    }

    // Equality comparisons with other ByteViews
    friend bool operator==(ByteView lhs, ByteView rhs) noexcept
    {
        return lhs.size() == rhs.size()
            && (lhs.data() == rhs.data() || std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0);
    }

    friend bool operator!=(ByteView lhs, ByteView rhs) noexcept
    {
        return !(lhs == rhs);
    }

    // Equality comparisons with std::string_view
    friend bool operator==(ByteView lhs, std::string_view rhs) noexcept
    {
        return lhs.asStringView() == rhs;
    }

    friend bool operator==(std::string_view lhs, ByteView rhs) noexcept
    {
        return lhs == rhs.asStringView();
    }

    friend bool operator!=(ByteView lhs, std::string_view rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator!=(std::string_view lhs, ByteView rhs) noexcept
    {
        return !(lhs == rhs);
    }

    // Equality comparisons with const char*
    friend bool operator==(ByteView lhs, const char* rhs) noexcept
    {
        return lhs.asStringView() == std::string_view(rhs);
    }

    friend bool operator==(const char* lhs, ByteView rhs) noexcept
    {
        return std::string_view(lhs) == rhs.asStringView();
    }

    friend bool operator!=(ByteView lhs, const char* rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator!=(const char* lhs, ByteView rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

} // namespace Common::Memory
