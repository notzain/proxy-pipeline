#pragma once

#include "common/memory/ByteView.hpp"
#include "common/memory/View.hpp"
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace Common::Memory {

namespace detail {

    template <typename T, typename = void>
    struct has_mutable_byte_data_and_size : std::false_type { };

    template <typename T>
    struct has_mutable_byte_data_and_size<T, std::void_t<decltype(std::declval<T&>().data()), decltype(std::declval<T&>().size())>> : std::true_type { };

    template <typename T>
    using mutable_byte_container_element_t = std::remove_pointer_t<decltype(std::declval<T&>().data())>;

} // namespace detail

/**
 * @brief High-performance, non-owning view of contiguous mutable bytes.
 *
 * Inherits from View<std::byte> (and transitively Common::Compat::span<std::byte>) for writing
 * into destination buffers (e.g. channel receive buffers, serialization destinations) with ergonomic
 * conversions from mutable containers, single mutable structs, and explicit/implicit conversion to immutable ByteView.
 */
class MutableByteView : public View<std::byte> {
public:
    using Base = View<std::byte>;
    using Base::Base;

    constexpr MutableByteView() noexcept = default;

    /**
     * @brief Constructs a MutableByteView from a mutable pointer and element count.
     */
    template <typename T,
        std::enable_if_t<!std::is_const_v<T>, int> = 0>
    constexpr MutableByteView(T* ptr, std::size_t count) noexcept
        : Base(reinterpret_cast<std::byte*>(ptr), count * sizeof(T))
    {
    }

    /**
     * @brief Constructs a MutableByteView from a mutable contiguous container (.data() and .size()).
     */
    template <typename Container,
        std::enable_if_t<
            detail::has_mutable_byte_data_and_size<Container>::value
                && !std::is_const_v<std::remove_reference_t<Container>>
                && !std::is_const_v<detail::mutable_byte_container_element_t<Container>>
                && !std::is_base_of_v<Base, std::decay_t<Container>>,
            int>
        = 0>
    constexpr MutableByteView(Container& container) noexcept
        : Base(
              reinterpret_cast<std::byte*>(container.data()),
              container.size() * sizeof(detail::mutable_byte_container_element_t<Container>))
    {
    }

    /**
     * @brief Constructs a MutableByteView from a fixed-size mutable C array.
     */
    template <typename T, std::size_t N,
        std::enable_if_t<!std::is_const_v<T>, int> = 0>
    constexpr MutableByteView(T (&arr)[N]) noexcept
        : Base(reinterpret_cast<std::byte*>(arr), N * sizeof(T))
    {
    }

    /**
     * @brief Constructs a MutableByteView from a single mutable trivially copyable object.
     */
    template <typename T,
        std::enable_if_t<
            std::is_trivially_copyable_v<T>
                && !std::is_const_v<T>
                && !detail::has_mutable_byte_data_and_size<T>::value
                && !std::is_pointer_v<T>
                && !std::is_array_v<T>
                && !std::is_base_of_v<Base, std::decay_t<T>>,
            int>
        = 0>
    constexpr explicit MutableByteView(T& object) noexcept
        : Base(reinterpret_cast<std::byte*>(std::addressof(object)), sizeof(T))
    {
    }

    /**
     * @brief Implicit conversion to immutable ByteView.
     */
    constexpr operator ByteView() const noexcept
    {
        return ByteView(data(), size());
    }

    /**
     * @brief Reinterprets the underlying pointer as char*.
     */
    [[nodiscard]] char* asChar() const noexcept
    {
        return reinterpret_cast<char*>(data());
    }

    /**
     * @brief Reinterprets the byte buffer as a typed mutable pointer.
     */
    template <typename T>
    [[nodiscard]] T* as() const noexcept
    {
        if (size() < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<T*>(data());
    }
};

} // namespace Common::Memory
