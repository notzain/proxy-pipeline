#pragma once

#include "common/compat/TypeTraits.hpp"
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Core::Serialization {

/**
 * @brief Represents a named member of a struct (field/element).
 */
template <typename ClassType, typename MemberType>
struct FieldDescriptor {
    std::string_view name;
    MemberType ClassType::* memberPtr;
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
 * @brief Universal customization point for type reflection.
 *
 * Specialize this struct for your custom POD types without modifying the struct itself:
 *
 * template <>
 * struct Core::Serialization::TypeTraits<MyPod> {
 *     static constexpr auto fields() {
 *         return std::make_tuple(
 *             Core::Serialization::field("foo", &MyPod::foo),
 *             Core::Serialization::field("id", &MyPod::id)
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

template <typename T>
inline constexpr bool ReflectedType = HasFields<T>;

} // namespace Core::Serialization
