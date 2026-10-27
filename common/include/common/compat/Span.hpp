#pragma once

#if __has_include(<version>)
#include <version>
#endif
#if __has_include(<span>)
#include <span>
#endif

#if defined(__cpp_lib_span) && (__cpp_lib_span >= 202002L)
#define PROXY_HAS_STD_SPAN 1
#endif

#if defined(PROXY_HAS_STD_SPAN)

namespace Common::Compat {

inline constexpr std::size_t dynamic_extent = std::dynamic_extent;

template <typename ElementType, std::size_t Extent = dynamic_extent>
using span = std::span<ElementType, Extent>;

using std::as_bytes;
using std::as_writable_bytes;

} // namespace Common::Compat

#else

#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <type_traits>

namespace Common::Compat {

inline constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

template <typename ElementType, std::size_t Extent = dynamic_extent>
class span;

namespace detail {

    template <typename T>
    struct is_span : std::false_type { };

    template <typename ElementType, std::size_t Extent>
    struct is_span<span<ElementType, Extent>> : std::true_type { };

    template <typename T>
    struct is_std_array : std::false_type { };

    template <typename T, std::size_t N>
    struct is_std_array<std::array<T, N>> : std::true_type { };

    template <typename Container, typename ElementType, typename = void>
    struct is_compatible_container : std::false_type { };

    template <typename Container, typename ElementType>
    struct is_compatible_container<
        Container,
        ElementType,
        std::void_t<
            std::enable_if_t<!is_span<std::remove_cv_t<Container>>::value>,
            std::enable_if_t<!is_std_array<std::remove_cv_t<Container>>::value>,
            std::enable_if_t<!std::is_array_v<std::remove_cv_t<Container>>>,
            decltype(std::data(std::declval<Container&>())),
            decltype(std::size(std::declval<Container&>())),
            std::enable_if_t<std::is_convertible_v<
                std::remove_pointer_t<decltype(std::data(std::declval<Container&>()))> (*)[],
                ElementType (*)[]>>>> : std::true_type { };

} // namespace detail

template <typename ElementType, std::size_t Extent>
class span {
public:
    using element_type = ElementType;
    using value_type = std::remove_cv_t<ElementType>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = ElementType*;
    using const_pointer = const ElementType*;
    using reference = ElementType&;
    using const_reference = const ElementType&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static constexpr size_type extent = Extent;

    template <std::size_t E = Extent, std::enable_if_t<E == 0 || E == dynamic_extent, int> = 0>
    constexpr span() noexcept
        : m_data(nullptr)
        , m_size(0)
    {
    }

    constexpr span(pointer first, size_type count) noexcept
        : m_data(first)
        , m_size(count)
    {
    }

    constexpr span(pointer first, pointer last) noexcept
        : m_data(first)
        , m_size(static_cast<size_type>(last - first))
    {
    }

    template <std::size_t N,
        std::enable_if_t<(Extent == dynamic_extent || Extent == N) && std::is_convertible_v<element_type (*)[], ElementType (*)[]>, int> = 0>
    constexpr span(element_type (&arr)[N]) noexcept
        : m_data(arr)
        , m_size(N)
    {
    }

    template <typename OtherElementType, std::size_t N,
        std::enable_if_t<(Extent == dynamic_extent || Extent == N) && std::is_convertible_v<OtherElementType (*)[], ElementType (*)[]>, int> = 0>
    constexpr span(std::array<OtherElementType, N>& arr) noexcept
        : m_data(arr.data())
        , m_size(N)
    {
    }

    template <typename OtherElementType, std::size_t N,
        std::enable_if_t<(Extent == dynamic_extent || Extent == N) && std::is_convertible_v<const OtherElementType (*)[], ElementType (*)[]>, int> = 0>
    constexpr span(const std::array<OtherElementType, N>& arr) noexcept
        : m_data(arr.data())
        , m_size(N)
    {
    }

    template <typename Container,
        std::enable_if_t<
            Extent == dynamic_extent && detail::is_compatible_container<Container, ElementType>::value, int> = 0>
    constexpr span(Container& cont)
        : m_data(std::data(cont))
        , m_size(std::size(cont))
    {
    }

    template <typename Container,
        std::enable_if_t<
            Extent == dynamic_extent && detail::is_compatible_container<const Container, ElementType>::value, int> = 0>
    constexpr span(const Container& cont)
        : m_data(std::data(cont))
        , m_size(std::size(cont))
    {
    }

    constexpr span(const span& other) noexcept = default;
    constexpr span& operator=(const span& other) noexcept = default;

    template <typename OtherElementType, std::size_t OtherExtent,
        std::enable_if_t<(Extent == dynamic_extent || Extent == OtherExtent) && std::is_convertible_v<OtherElementType (*)[], ElementType (*)[]>, int> = 0>
    constexpr span(const span<OtherElementType, OtherExtent>& other) noexcept
        : m_data(other.data())
        , m_size(other.size())
    {
    }

    [[nodiscard]] constexpr iterator begin() const noexcept { return m_data; }
    [[nodiscard]] constexpr iterator end() const noexcept { return m_data + m_size; }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_data; }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return m_data + m_size; }
    [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    [[nodiscard]] constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

    [[nodiscard]] constexpr reference front() const noexcept { return m_data[0]; }
    [[nodiscard]] constexpr reference back() const noexcept { return m_data[m_size - 1]; }
    [[nodiscard]] constexpr reference operator[](size_type idx) const noexcept { return m_data[idx]; }
    [[nodiscard]] constexpr pointer data() const noexcept { return m_data; }

    [[nodiscard]] constexpr size_type size() const noexcept { return m_size; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return m_size * sizeof(ElementType); }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

    template <std::size_t Count>
    [[nodiscard]] constexpr span<ElementType, Count> first() const
    {
        return span<ElementType, Count>(m_data, Count);
    }

    [[nodiscard]] constexpr span<ElementType, dynamic_extent> first(size_type count) const
    {
        return span<ElementType, dynamic_extent>(m_data, count);
    }

    template <std::size_t Count>
    [[nodiscard]] constexpr span<ElementType, Count> last() const
    {
        return span<ElementType, Count>(m_data + (m_size - Count), Count);
    }

    [[nodiscard]] constexpr span<ElementType, dynamic_extent> last(size_type count) const
    {
        return span<ElementType, dynamic_extent>(m_data + (m_size - count), count);
    }

    template <std::size_t Offset, std::size_t Count = dynamic_extent>
    [[nodiscard]] constexpr auto subspan() const
    {
        constexpr std::size_t ResultExtent = (Count != dynamic_extent) ? Count : (Extent != dynamic_extent ? Extent - Offset : dynamic_extent);
        return span<ElementType, ResultExtent>(
            m_data + Offset,
            Count == dynamic_extent ? m_size - Offset : Count);
    }

    [[nodiscard]] constexpr span<ElementType, dynamic_extent> subspan(
        size_type offset,
        size_type count = dynamic_extent) const
    {
        return span<ElementType, dynamic_extent>(
            m_data + offset,
            count == dynamic_extent ? m_size - offset : count);
    }

private:
    pointer m_data { nullptr };
    size_type m_size { 0 };
};

// Deduction guides
template <typename T, std::size_t N>
span(T (&)[N]) -> span<T, N>;

template <typename T, std::size_t N>
span(std::array<T, N>&) -> span<T, N>;

template <typename T, std::size_t N>
span(const std::array<T, N>&) -> span<const T, N>;

template <typename Container>
span(Container&) -> span<typename Container::value_type>;

template <typename Container>
span(const Container&) -> span<const typename Container::value_type>;

template <typename ElementType, std::size_t Extent>
span<const std::byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
as_bytes(span<ElementType, Extent> s) noexcept
{
    return span<const std::byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>(
        reinterpret_cast<const std::byte*>(s.data()),
        s.size_bytes());
}

template <typename ElementType, std::size_t Extent,
    std::enable_if_t<!std::is_const_v<ElementType>, int> = 0>
span<std::byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>
as_writable_bytes(span<ElementType, Extent> s) noexcept
{
    return span<std::byte, Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent>(
        reinterpret_cast<std::byte*>(s.data()),
        s.size_bytes());
}

} // namespace Common::Compat

#endif
