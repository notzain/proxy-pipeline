#pragma once

#include "common/compat/TypeTraits.hpp"
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Core::Serialization {

/**
 * @brief Represents a named member of a struct (element/field).
 */
template <typename ClassType, typename MemberType>
struct FieldDescriptor {
    std::string_view name;
    MemberType ClassType::* memberPtr;
    static constexpr bool isAttribute = false;
};

/**
 * @brief Represents an XML attribute member of a struct.
 */
template <typename ClassType, typename MemberType>
struct AttributeDescriptor {
    std::string_view name;
    MemberType ClassType::* memberPtr;
    static constexpr bool isAttribute = true;
};

/**
 * @brief Helper to construct a FieldDescriptor with template deduction.
 */
template <typename ClassType, typename MemberType>
constexpr auto field(std::string_view name, MemberType ClassType::* memberPtr)
{
    return FieldDescriptor<ClassType, MemberType> { name, memberPtr };
}

/**
 * @brief Helper to construct an AttributeDescriptor with template deduction for XML attributes.
 */
template <typename ClassType, typename MemberType>
constexpr auto attribute(std::string_view name, MemberType ClassType::* memberPtr)
{
    return AttributeDescriptor<ClassType, MemberType> { name, memberPtr };
}

/**
 * @brief Customization point for type traits.
 *
 * Specialize this struct for your custom POD types without modifying the struct itself:
 *
 * template <>
 * struct Core::Serialization::TypeTraits<MyPod> {
 *     // General fields (or XML child elements)
 *     static constexpr auto fields() {
 *         return std::make_tuple(
 *             Core::Serialization::field("foo", &MyPod::foo)
 *         );
 *     }
 *
 *     // Optional XML attributes
 *     static constexpr auto attributes() {
 *         return std::make_tuple(
 *             Core::Serialization::attribute("id", &MyPod::id)
 *         );
 *     }
 * };
 */
template <typename T, typename = void>
struct TypeTraits {
    static constexpr bool isReflected = false;
};

template <typename T, typename = void>
struct HasFieldsHelper : std::false_type { };

template <typename T>
struct HasFieldsHelper<T, std::void_t<decltype(TypeTraits<Common::Compat::remove_cvref_t<T>>::fields())>> : std::true_type { };

template <typename T>
inline constexpr bool HasFields = HasFieldsHelper<T>::value;

template <typename T, typename = void>
struct HasAttributesHelper : std::false_type { };

template <typename T>
struct HasAttributesHelper<T, std::void_t<decltype(TypeTraits<Common::Compat::remove_cvref_t<T>>::attributes())>> : std::true_type { };

template <typename T>
inline constexpr bool HasAttributes = HasAttributesHelper<T>::value;

template <typename T>
inline constexpr bool ReflectedType = HasFields<T> || HasAttributes<T>;

} // namespace Core::Serialization
