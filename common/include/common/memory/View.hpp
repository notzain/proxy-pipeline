#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>

namespace Common::Memory {

namespace detail {

    template <typename T, typename = void>
    struct has_typed_data_and_size : std::false_type { };

    template <typename T>
    struct has_typed_data_and_size<T, std::void_t<decltype(std::declval<T&>().data()), decltype(std::declval<T&>().size())>> : std::true_type { };

    template <typename T>
    using typed_container_element_t = std::remove_pointer_t<decltype(std::declval<T&>().data())>;

} // namespace detail

/**
 * @brief High-performance, generic non-owning view of contiguous elements of type ElementType.
 *
 * Inherits from Common::Compat::span<ElementType> for 100% standard span compatibility,
 * while providing ergonomic constructors for typed contiguous containers (e.g. std::vector,
 * std::array), raw pointer/length pairs, C arrays, and safe covariance from View<T> to View<const T>.
 *
 * @tparam ElementType The type of elements in the contiguous sequence.
 */
template <typename ElementType>
class View : public Common::Compat::span<ElementType> {
public:
    using Base = Common::Compat::span<ElementType>;
    using Base::Base;

    constexpr View() noexcept = default;

    /**
     * @brief Constructs a View from a typed pointer and element count.
     */
    constexpr View(ElementType* ptr, std::size_t count) noexcept
        : Base(ptr, count)
    {
    }

    /**
     * @brief Constructs a View from a typed contiguous container.
     */
    template <typename Container,
        std::enable_if_t<
            detail::has_typed_data_and_size<Container>::value
                && std::is_convertible_v<detail::typed_container_element_t<Container> (*)[], ElementType (*)[]>
                && !std::is_base_of_v<Base, std::decay_t<Container>>,
            int> = 0>
    constexpr View(Container&& container) noexcept
        : Base(container.data(), container.size())
    {
    }

    /**
     * @brief Constructs a View from a fixed-size typed C array.
     */
    template <std::size_t N>
    constexpr View(ElementType (&arr)[N]) noexcept
        : Base(arr, N)
    {
    }

    /**
     * @brief Covariant copy constructor enabling View<T> to convert implicitly to View<const T>.
     */
    template <typename OtherType,
        std::enable_if_t<
            std::is_same_v<ElementType, const OtherType>,
            int> = 0>
    constexpr View(const View<OtherType>& other) noexcept
        : Base(other.data(), other.size())
    {
    }
};

} // namespace Common::Memory

