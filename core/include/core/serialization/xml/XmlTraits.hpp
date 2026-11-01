#pragma once

#include "core/serialization/TypeTraits.hpp"

#include <tuple>
#include <type_traits>

namespace Core::Serialization::Xml {

/**
 * @brief Compile-time configuration builder for XML serialization behavior.
 *
 * Stores member pointers that should be serialized as XML node attributes
 * rather than child elements.
 *
 * @tparam ClassType The struct/class type being configured.
 * @tparam MemberPtrs Member pointers configured as XML attributes.
 */
template <typename ClassType, typename... MemberPtrs>
class XmlConfig {
public:
    std::tuple<MemberPtrs...> attributePtrs;

    constexpr XmlConfig() = default;

    constexpr explicit XmlConfig(std::tuple<MemberPtrs...> ptrs)
        : attributePtrs(ptrs)
    {
    }

    /**
     * @brief Fluently marks a member pointer as an XML attribute.
     *
     * @tparam MemberType Type of the member variable.
     * @param ptr Member pointer to mark as an attribute.
     * @return XmlConfig with ptr appended to the attribute list.
     */
    template <typename MemberType>
    constexpr auto asAttribute(MemberType ClassType::* ptr) const noexcept
    {
        return XmlConfig<ClassType, MemberPtrs..., MemberType ClassType::*>(
            std::tuple_cat(attributePtrs, std::make_tuple(ptr)));
    }

    /**
     * @brief Checks whether the given member pointer is configured as an attribute.
     *
     * @tparam MemberType Type of the member variable.
     * @param ptr Member pointer to test.
     * @return true if ptr matches one of the configured attribute member pointers, false otherwise.
     */
    template <typename MemberType>
    [[nodiscard]] constexpr bool isAttribute(MemberType ClassType::* ptr) const noexcept
    {
        if constexpr (sizeof...(MemberPtrs) == 0) {
            return false;
        } else {
            return std::apply([ptr](auto... storedPtr) {
                auto matches = [ptr](auto sPtr) {
                    if constexpr (std::is_same_v<decltype(ptr), decltype(sPtr)>) {
                        return ptr == sPtr;
                    } else {
                        return false;
                    }
                };
                return (matches(storedPtr) || ...);
            }, attributePtrs);
        }
    }
};

/**
 * @brief XML-specific traits customization point.
 *
 * Specialize this struct for types requiring XML-specific mappings (such as XML attributes).
 * If not specialized, defaults to an empty XmlConfig (all fields serialized as child elements).
 *
 * Example:
 * @code
 * template <>
 * struct Core::Serialization::Xml::XmlTraits<UserProfile> {
 *     static constexpr auto configure() {
 *         return XmlConfig<UserProfile>{}
 *             .asAttribute(&UserProfile::id)
 *             .asAttribute(&UserProfile::role);
 *     }
 * };
 * @endcode
 *
 * @tparam T Type to inspect.
 */
template <typename T, typename = void>
struct XmlTraits {
    static constexpr auto configure() noexcept
    {
        return XmlConfig<T> { };
    }
};

} // namespace Core::Serialization::Xml
