#pragma once

#include "core/serialization/DebugTraits.hpp"
#include "core/serialization/TypeTraits.hpp"
#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

#include <string_view>
#include <tuple>
#include <type_traits>

namespace Core::Serialization::detail {

struct DebugFormatOptions {
    bool pretty = false;
    bool color = false;
    unsigned int indent = 0;
    unsigned int indentSpaces = 2;
};

template <typename OutputIt>
void writeIndent(OutputIt& out, unsigned int level, unsigned int spaces)
{
    for (unsigned int i = 0; i < level * spaces; ++i) {
        *out++ = ' ';
    }
}

template <typename T, typename OutputIt>
void formatValue(const T& value, OutputIt& out, const DebugFormatOptions& opts);

template <typename StructT, typename OutputIt>
void formatReflectedStruct(const StructT& value, OutputIt& out, const DebugFormatOptions& opts)
{
    constexpr std::string_view typeName = DebugName<StructT>;

    if (!typeName.empty()) {
        if (opts.color) {
            out = fmt::format_to(out, fmt::emphasis::bold | fg(fmt::color::cyan), "{}", typeName);
            *out++ = ' ';
        } else {
            out = fmt::format_to(out, "{} ", typeName);
        }
    }

    if (opts.pretty) {
        *out++ = '{';
        *out++ = '\n';

        DebugFormatOptions nestedOpts = opts;
        nestedOpts.indent = opts.indent + 1;

        bool first = true;
        std::apply([&](const auto&... fieldDescs) {
            ([&](const auto& desc) {
                if (!first) {
                    *out++ = ',';
                    *out++ = '\n';
                }
                first = false;

                writeIndent(out, nestedOpts.indent, nestedOpts.indentSpaces);

                if (nestedOpts.color) {
                    out = fmt::format_to(out, fg(fmt::color::yellow), "{}", desc.name);
                    out = fmt::format_to(out, ": ");
                } else {
                    out = fmt::format_to(out, "{}: ", desc.name);
                }

                formatValue(value.*(desc.memberPtr), out, nestedOpts);
            }(fieldDescs),
                ...);
        },
            TypeTraits<StructT>::fields());

        *out++ = '\n';
        writeIndent(out, opts.indent, opts.indentSpaces);
        *out++ = '}';
    } else {
        *out++ = '{';
        *out++ = ' ';

        bool first = true;
        std::apply([&](const auto&... fieldDescs) {
            ([&](const auto& desc) {
                if (!first) {
                    *out++ = ',';
                    *out++ = ' ';
                }
                first = false;

                if (opts.color) {
                    out = fmt::format_to(out, fg(fmt::color::yellow), "{}", desc.name);
                    out = fmt::format_to(out, ": ");
                } else {
                    out = fmt::format_to(out, "{}: ", desc.name);
                }

                formatValue(value.*(desc.memberPtr), out, opts);
            }(fieldDescs),
                ...);
        },
            TypeTraits<StructT>::fields());

        *out++ = ' ';
        *out++ = '}';
    }
}

template <typename T, typename OutputIt>
void formatValue(const T& value, OutputIt& out, const DebugFormatOptions& opts)
{
    using CleanT = Common::Compat::remove_cvref_t<T>;

    if constexpr (HasFields<CleanT>) {
        formatReflectedStruct(value, out, opts);
    } else if constexpr (std::is_same_v<CleanT, std::string> || std::is_same_v<CleanT, std::string_view>) {
        if (opts.color) {
            out = fmt::format_to(out, fg(fmt::color::green), "\"{}\"", value);
        } else {
            out = fmt::format_to(out, "\"{}\"", value);
        }
    } else if constexpr (std::is_integral_v<CleanT> || std::is_floating_point_v<CleanT>) {
        if (opts.color) {
            out = fmt::format_to(out, fg(fmt::color::light_blue), "{}", value);
        } else {
            out = fmt::format_to(out, "{}", value);
        }
    } else if constexpr (std::is_same_v<CleanT, bool>) {
        if (opts.color) {
            out = fmt::format_to(out, fg(fmt::color::magenta), "{}", value ? "true" : "false");
        } else {
            out = fmt::format_to(out, "{}", value ? "true" : "false");
        }
    } else {
        out = fmt::format_to(out, "{}", value);
    }
}

} // namespace Core::Serialization::detail

/**
 * @brief Automatic fmt::formatter for any type implementing Core::Serialization::TypeTraits.
 *
 * Supported format specifiers:
 * - "" (default): Uses DebugTraits<T> defaults (typically compact inline representation).
 * - "pretty" or "#": Forces multi-line indented tree layout.
 * - "compact": Forces compact single-line layout.
 * - "color": Forces ANSI terminal coloring.
 * - combinations: e.g. "{:#c}" or "{:pretty,color}" or "{:color}".
 */
template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<Core::Serialization::HasFields<T>>> {
    bool pretty = Core::Serialization::DebugMultiline<T>;
    bool color = Core::Serialization::DebugColor<T>;
    unsigned int indentSpaces = Core::Serialization::DebugIndentSpaces<T>;

    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx)
    {
        auto it = ctx.begin();
        auto end = ctx.end();

        while (it != end && *it != '}') {
            if (*it == '#') {
                pretty = true;
                ++it;
            } else if (*it == 'c' && (it + 1 == end || *(it + 1) == '}' || *(it + 1) == ',' || *(it + 1) == ' ')) {
                color = true;
                ++it;
            } else if (it + 6 <= end && it[0] == 'p' && it[1] == 'r' && it[2] == 'e' && it[3] == 't' && it[4] == 't' && it[5] == 'y') {
                pretty = true;
                it += 6;
            } else if (it + 7 <= end && it[0] == 'c' && it[1] == 'o' && it[2] == 'm' && it[3] == 'p' && it[4] == 'a' && it[5] == 'c' && it[6] == 't') {
                pretty = false;
                it += 7;
            } else if (it + 5 <= end && it[0] == 'c' && it[1] == 'o' && it[2] == 'l' && it[3] == 'o' && it[4] == 'r') {
                color = true;
                it += 5;
            } else if (*it == ',' || *it == ' ') {
                ++it;
            } else {
                throw fmt::format_error("unknown format specifier for reflected type");
            }
        }
        return it;
    }

    template <typename FormatContext>
    auto format(const T& value, FormatContext& ctx) const
    {
        auto out = ctx.out();
        Core::Serialization::detail::DebugFormatOptions opts {
            .pretty = pretty,
            .color = color,
            .indent = 0,
            .indentSpaces = indentSpaces
        };
        Core::Serialization::detail::formatReflectedStruct(value, out, opts);
        return out;
    }
};
