#pragma once

#include "log/Structured.hpp"
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Core::Serialization {
template <typename T, typename Enable>
struct TypeTraits;
}

namespace Proxy::Log::Detail {

template <typename T, typename = void>
struct HasCustomFields : std::false_type { };

template <typename T>
struct HasCustomFields<T, std::void_t<decltype(T::fields())>> : std::true_type { };

template <typename T, typename = void>
struct HasSerializationTypeTraits : std::false_type { };

template <typename T>
struct HasSerializationTypeTraits<T, std::void_t<decltype(::Core::Serialization::TypeTraits<T, void>::fields())>> : std::true_type { };

template <typename T>
inline constexpr bool IsReflectedEntity = HasCustomFields<std::decay_t<T>>::value || HasSerializationTypeTraits<std::decay_t<T>>::value;

template <typename OutputIt, typename T>
void writeJsonValue(OutputIt& out, const T& val);

template <typename OutputIt, typename StructT, typename FieldsTuple>
void writeJsonStruct(OutputIt& out, const StructT& val, const FieldsTuple& fields)
{
    *out++ = '{';
    bool first = true;
    std::apply([&](const auto&... fieldDescs) {
        ([&](const auto& desc) {
            if (!first) {
                *out++ = ',';
            }
            first = false;
            *out++ = '"';
            for (char c : desc.name) {
                *out++ = c;
            }
            *out++ = '"';
            *out++ = ':';
            writeJsonValue(out, val.*(desc.memberPtr));
        }(fieldDescs),
            ...);
    },
        fields);
    *out++ = '}';
}

template <typename OutputIt, typename T>
void writeJsonValue(OutputIt& out, const T& val)
{
    using CleanT = std::decay_t<T>;

    if constexpr (std::is_same_v<CleanT, bool>) {
        if (val) {
            out = fmt::format_to(out, "true");
        } else {
            out = fmt::format_to(out, "false");
        }
    } else if constexpr (std::is_integral_v<CleanT> || std::is_floating_point_v<CleanT>) {
        out = fmt::format_to(out, "{}", val);
    } else if constexpr (std::is_same_v<CleanT, std::string> || std::is_same_v<CleanT, std::string_view> || std::is_same_v<CleanT, const char*>) {
        std::string_view sv(val);
        *out++ = '"';
        for (char c : sv) {
            if (c == '"') {
                *out++ = '\\';
                *out++ = '"';
            } else if (c == '\\') {
                *out++ = '\\';
                *out++ = '\\';
            } else if (c == '\n') {
                *out++ = '\\';
                *out++ = 'n';
            } else if (c == '\r') {
                *out++ = '\\';
                *out++ = 'r';
            } else if (c == '\t') {
                *out++ = '\\';
                *out++ = 't';
            } else {
                *out++ = c;
            }
        }
        *out++ = '"';
    } else if constexpr (HasCustomFields<CleanT>::value) {
        writeJsonStruct(out, val, CleanT::fields());
    } else if constexpr (HasSerializationTypeTraits<CleanT>::value) {
        writeJsonStruct(out, val, ::Core::Serialization::TypeTraits<CleanT, void>::fields());
    } else {
        out = fmt::format_to(out, "\"{}\"", val);
    }
}

template <typename T>
std::string serializeReflectedToJson(const T& val)
{
    using CleanT = std::decay_t<T>;
    fmt::memory_buffer buf;
    auto out = std::back_inserter(buf);
    if constexpr (HasCustomFields<CleanT>::value) {
        writeJsonStruct(out, val, CleanT::fields());
    } else if constexpr (HasSerializationTypeTraits<CleanT>::value) {
        writeJsonStruct(out, val, ::Core::Serialization::TypeTraits<CleanT, void>::fields());
    } else {
        writeJsonValue(out, val);
    }
    return std::string(buf.data(), buf.size());
}

} // namespace Proxy::Log::Detail
