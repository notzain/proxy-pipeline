#pragma once

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

template <typename T>
concept HasFields = requires {
    { TypeTraits<std::remove_cvref_t<T>>::fields() };
};

template <typename T>
concept HasAttributes = requires {
    { TypeTraits<std::remove_cvref_t<T>>::attributes() };
};

template <typename T>
concept ReflectedType = HasFields<T> || HasAttributes<T>;

} // namespace Core::Serialization
