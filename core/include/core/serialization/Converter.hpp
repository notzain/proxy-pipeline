#pragma once

#include "core/serialization/TypeTraits.hpp"
#include "core/serialization/Value.hpp"

#include <array>
#include <charconv>
#include <concepts>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Core::Serialization {

// Forward declarations
template <typename T>
Value toIntermediate(const T& value);

template <typename T>
std::optional<T> fromIntermediate(const Value& value);

namespace Detail {

    template <typename T>
    struct IsOptional : std::false_type { };

    template <typename T>
    struct IsOptional<std::optional<T>> : std::true_type { };

    template <typename T>
    inline constexpr bool isOptionalV = IsOptional<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct IsVector : std::false_type { };

    template <typename T, typename Alloc>
    struct IsVector<std::vector<T, Alloc>> : std::true_type { };

    template <typename T>
    inline constexpr bool isVectorV = IsVector<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct IsStdArray : std::false_type { };

    template <typename T, std::size_t N>
    struct IsStdArray<std::array<T, N>> : std::true_type { };

    template <typename T>
    inline constexpr bool isStdArrayV = IsStdArray<std::remove_cvref_t<T>>::value;

    template <typename T>
    struct IsMap : std::false_type { };

    template <typename Val, typename Compare, typename Alloc>
    struct IsMap<std::map<std::string, Val, Compare, Alloc>> : std::true_type { };

    template <typename Val, typename Hash, typename KeyEqual, typename Alloc>
    struct IsMap<std::unordered_map<std::string, Val, Hash, KeyEqual, Alloc>> : std::true_type { };

    template <typename T>
    inline constexpr bool isMapV = IsMap<std::remove_cvref_t<T>>::value;

    // Helper to stringify an attribute value
    template <typename T>
    std::string attributeToString(const T& val)
    {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, std::string>) {
            return val;
        } else if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
            return std::string(val);
        } else if constexpr (std::is_same_v<CleanT, bool>) {
            return val ? "true" : "false";
        } else if constexpr (std::is_arithmetic_v<CleanT>) {
            return std::to_string(val);
        } else if constexpr (isOptionalV<CleanT>) {
            if (val.has_value()) {
                return attributeToString(*val);
            }
            return "";
        } else {
            std::ostringstream oss;
            oss << val;
            return oss.str();
        }
    }

    // Helper to parse an attribute string into a type
    template <typename T>
    std::optional<T> attributeFromString(std::string_view str)
    {
        using CleanT = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<CleanT, std::string>) {
            return std::string(str);
        } else if constexpr (std::is_same_v<CleanT, bool>) {
            if (str == "true" || str == "1")
                return true;
            if (str == "false" || str == "0")
                return false;
            return std::nullopt;
        } else if constexpr (std::is_integral_v<CleanT>) {
            CleanT val { };
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
            if (ec == std::errc()) {
                return val;
            }
            return std::nullopt;
        } else if constexpr (std::is_floating_point_v<CleanT>) {
            try {
                return static_cast<CleanT>(std::stod(std::string(str)));
            } catch (...) {
                return std::nullopt;
            }
        } else if constexpr (isOptionalV<CleanT>) {
            using ValueType = typename CleanT::value_type;
            auto inner = attributeFromString<ValueType>(str);
            if (!inner) {
                return std::nullopt;
            }
            return CleanT { std::move(*inner) };
        } else {
            return std::nullopt;
        }
    }

} // namespace Detail

// --- toIntermediate implementations ---

template <typename T>
Value toIntermediate(const T& value)
{
    using CleanT = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<CleanT, Value>) {
        return value;
    } else if constexpr (std::is_same_v<CleanT, bool>) {
        return Value(value);
    } else if constexpr (std::is_integral_v<CleanT>) {
        if constexpr (std::is_unsigned_v<CleanT>) {
            return Value(static_cast<std::uint64_t>(value));
        } else {
            return Value(static_cast<std::int64_t>(value));
        }
    } else if constexpr (std::is_floating_point_v<CleanT>) {
        return Value(static_cast<double>(value));
    } else if constexpr (std::is_convertible_v<CleanT, std::string_view>) {
        return Value(std::string(value));
    } else if constexpr (Detail::isOptionalV<CleanT>) {
        if (value.has_value()) {
            return toIntermediate(*value);
        }
        return Value(NullType { });
    } else if constexpr (Detail::isVectorV<CleanT> || Detail::isStdArrayV<CleanT>) {
        ArrayType arr;
        arr.reserve(value.size());
        for (const auto& item : value) {
            arr.push_back(toIntermediate(item));
        }
        return Value(std::move(arr));
    } else if constexpr (Detail::isMapV<CleanT>) {
        ObjectType obj;
        for (const auto& [k, v] : value) {
            obj[k] = toIntermediate(v);
        }
        return Value(std::move(obj));
    } else if constexpr (ReflectedType<CleanT>) {
        ObjectType obj;

        if constexpr (HasFields<CleanT>) {
            constexpr auto fieldTuple = TypeTraits<CleanT>::fields();
            std::apply([&](const auto&... fieldDesc) {
                ((obj[std::string(fieldDesc.name)] = toIntermediate(value.*(fieldDesc.memberPtr))), ...);
            },
                fieldTuple);
        }

        Value ir(std::move(obj));

        if constexpr (HasAttributes<CleanT>) {
            constexpr auto attrTuple = TypeTraits<CleanT>::attributes();
            std::apply([&](const auto&... attrDesc) {
                auto setAttr = [&](const auto& desc) {
                    using MemberType = std::remove_reference_t<decltype(value.*(desc.memberPtr))>;
                    if constexpr (Detail::isOptionalV<MemberType>) {
                        if ((value.*(desc.memberPtr)).has_value()) {
                            ir.setAttribute(std::string(desc.name), Detail::attributeToString(*(value.*(desc.memberPtr))));
                        }
                    } else {
                        ir.setAttribute(std::string(desc.name), Detail::attributeToString(value.*(desc.memberPtr)));
                    }
                };
                (setAttr(attrDesc), ...);
            },
                attrTuple);
        }

        return ir;
    } else {
        static_assert(sizeof(CleanT) == 0, "Type T is not supported for toIntermediate. Consider specializing Core::Serialization::TypeTraits<T>.");
    }
}

// --- fromIntermediate implementations ---

template <typename T>
std::optional<T> fromIntermediate(const Value& value)
{
    using CleanT = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<CleanT, Value>) {
        return value;
    } else if constexpr (std::is_same_v<CleanT, bool>) {
        return value.asBool();
    } else if constexpr (std::is_integral_v<CleanT>) {
        if constexpr (std::is_unsigned_v<CleanT>) {
            auto uval = value.asUInt();
            if (!uval) {
                return std::nullopt;
            }
            return static_cast<CleanT>(*uval);
        } else {
            auto ival = value.asInt();
            if (!ival) {
                return std::nullopt;
            }
            return static_cast<CleanT>(*ival);
        }
    } else if constexpr (std::is_floating_point_v<CleanT>) {
        auto dval = value.asDouble();
        if (!dval) {
            return std::nullopt;
        }
        return static_cast<CleanT>(*dval);
    } else if constexpr (std::is_same_v<CleanT, std::string>) {
        if (auto s = value.asString()) {
            return *s;
        }
        return std::nullopt;
    } else if constexpr (Detail::isOptionalV<CleanT>) {
        using ValueType = typename CleanT::value_type;
        if (value.isNull()) {
            return CleanT { std::nullopt };
        }
        auto inner = fromIntermediate<ValueType>(value);
        if (!inner) {
            return std::nullopt;
        }
        return CleanT { std::move(*inner) };
    } else if constexpr (Detail::isVectorV<CleanT>) {
        using ElemType = typename CleanT::value_type;
        const auto* arr = value.asArray();
        if (!arr) {
            return std::nullopt;
        }
        CleanT result;
        result.reserve(arr->size());
        for (const auto& item : *arr) {
            auto parsed = fromIntermediate<ElemType>(item);
            if (!parsed) {
                return std::nullopt;
            }
            result.push_back(std::move(*parsed));
        }
        return result;
    } else if constexpr (Detail::isStdArrayV<CleanT>) {
        using ElemType = typename CleanT::value_type;
        constexpr std::size_t N = std::tuple_size_v<CleanT>;
        const auto* arr = value.asArray();
        if (!arr || arr->size() != N) {
            return std::nullopt;
        }
        CleanT result;
        for (std::size_t i = 0; i < N; ++i) {
            auto parsed = fromIntermediate<ElemType>((*arr)[i]);
            if (!parsed) {
                return std::nullopt;
            }
            result[i] = std::move(*parsed);
        }
        return result;
    } else if constexpr (Detail::isMapV<CleanT>) {
        using ElemType = typename CleanT::mapped_type;
        const auto* obj = value.asObject();
        if (!obj) {
            return std::nullopt;
        }
        CleanT result;
        for (const auto& [k, v] : *obj) {
            auto parsed = fromIntermediate<ElemType>(v);
            if (!parsed) {
                return std::nullopt;
            }
            result[k] = std::move(*parsed);
        }
        return result;
    } else if constexpr (ReflectedType<CleanT>) {
        const auto* obj = value.asObject();
        if (!obj && !value.isObject()) {
            return std::nullopt;
        }
        CleanT result { };
        bool allParsed = true;

        if constexpr (HasFields<CleanT>) {
            constexpr auto fieldTuple = TypeTraits<CleanT>::fields();
            std::apply([&](const auto&... fieldDesc) {
                auto parseField = [&](const auto& desc) {
                    if (!allParsed) {
                        return;
                    }
                    using MemberType = std::remove_reference_t<decltype(result.*(desc.memberPtr))>;
                    if (obj) {
                        if (auto it = obj->find(std::string(desc.name)); it != obj->end()) {
                            auto parsed = fromIntermediate<MemberType>(it->second);
                            if (parsed) {
                                result.*(desc.memberPtr) = std::move(*parsed);
                                return;
                            } else {
                                allParsed = false;
                                return;
                            }
                        }
                    }
                    if constexpr (Detail::isOptionalV<MemberType>) {
                        result.*(desc.memberPtr) = std::nullopt;
                    } else {
                        allParsed = false;
                    }
                };
                (parseField(fieldDesc), ...);
            },
                fieldTuple);
        }

        if constexpr (HasAttributes<CleanT>) {
            constexpr auto attrTuple = TypeTraits<CleanT>::attributes();
            std::apply([&](const auto&... attrDesc) {
                auto parseAttr = [&](const auto& desc) {
                    if (!allParsed) {
                        return;
                    }
                    using MemberType = std::remove_reference_t<decltype(result.*(desc.memberPtr))>;
                    if (const auto* attrVal = value.findAttribute(desc.name)) {
                        auto parsed = Detail::attributeFromString<MemberType>(*attrVal);
                        if (parsed) {
                            result.*(desc.memberPtr) = std::move(*parsed);
                            return;
                        } else {
                            allParsed = false;
                            return;
                        }
                    }
                    if constexpr (Detail::isOptionalV<MemberType>) {
                        result.*(desc.memberPtr) = std::nullopt;
                    } else {
                        allParsed = false;
                    }
                };
                (parseAttr(attrDesc), ...);
            },
                attrTuple);
        }

        if (!allParsed) {
            return std::nullopt;
        }
        return result;
    } else {
        static_assert(sizeof(CleanT) == 0, "Type T is not supported for fromIntermediate. Consider specializing Core::Serialization::TypeTraits<T>.");
    }
}

} // namespace Core::Serialization
