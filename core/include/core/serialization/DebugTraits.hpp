#pragma once

#include "core/serialization/TypeTraits.hpp"
#include <string_view>
#include <type_traits>

namespace Core::Serialization {

/**
 * @brief Base definition holding fallback configurations for DebugTraits.
 */
struct DebugTraitsBase {
    static constexpr std::string_view name = "";
    static constexpr bool multiline = false;
    static constexpr bool color = false;
    static constexpr unsigned int indentSpaces = 2;
};

/**
 * @brief Configuration and display traits for debug formatting.
 *
 * Specialize this trait to customize how a reflected struct or type is printed:
 *
 * template <>
 * struct Core::Serialization::DebugTraits<MyPod> : Core::Serialization::DebugTraitsBase {
 *     static constexpr std::string_view name = "MyPod";
 *     static constexpr bool multiline = true;
 * };
 */
template <typename T, typename = void>
struct DebugTraits : DebugTraitsBase { };

namespace detail {

    template <typename T, typename = void>
    struct TypeNameHelper {
        static constexpr std::string_view value = DebugTraits<T>::name;
    };

    template <typename T, typename = void>
    struct MultilineHelper : std::bool_constant<DebugTraitsBase::multiline> { };

    template <typename T>
    struct MultilineHelper<T, std::void_t<decltype(DebugTraits<T>::multiline)>>
        : std::bool_constant<DebugTraits<T>::multiline> { };

    template <typename T, typename = void>
    struct ColorHelper : std::bool_constant<DebugTraitsBase::color> { };

    template <typename T>
    struct ColorHelper<T, std::void_t<decltype(DebugTraits<T>::color)>>
        : std::bool_constant<DebugTraits<T>::color> { };

    template <typename T, typename = void>
    struct IndentHelper {
        static constexpr unsigned int value = DebugTraitsBase::indentSpaces;
    };

    template <typename T>
    struct IndentHelper<T, std::void_t<decltype(DebugTraits<T>::indentSpaces)>> {
        static constexpr unsigned int value = DebugTraits<T>::indentSpaces;
    };

} // namespace detail

template <typename T>
inline constexpr std::string_view DebugName = detail::TypeNameHelper<T>::value;

template <typename T>
inline constexpr bool DebugMultiline = detail::MultilineHelper<T>::value;

template <typename T>
inline constexpr bool DebugColor = detail::ColorHelper<T>::value;

template <typename T>
inline constexpr unsigned int DebugIndentSpaces = detail::IndentHelper<T>::value;

} // namespace Core::Serialization
