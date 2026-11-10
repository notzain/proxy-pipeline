#pragma once

#include "log/ComponentTag.hpp"
#include "log/JsonValueWriter.hpp"
#include "log/Structured.hpp"
#include <array>
#include <fmt/format.h>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef PROXY_LOG_TAG
#define PROXY_LOG_CURRENT_COMPONENT ::Proxy::Log::Detail::extractStem(__FILE__)
#else
#define PROXY_LOG_CURRENT_COMPONENT std::string_view(PROXY_LOG_TAG)
#endif

namespace Proxy::Log {
bool shouldLog(Level level) noexcept;
void logStructured(const StructuredLogRecord& record);
}

namespace Proxy::Log::Detail {

template <size_t N>
struct StaticString {
    char data[N] { };
    static constexpr size_t size = N;

    constexpr StaticString(const char (&str)[N]) noexcept
    {
        for (size_t i = 0; i < N; ++i) {
            data[i] = str[i];
        }
    }

    [[nodiscard]] constexpr size_t countNamedPlaceholders() const noexcept
    {
        size_t count = 0;
        for (size_t i = 0; i + 1 < N; ++i) {
            if (data[i] == '{') {
                if (data[i + 1] == '{') {
                    ++i;
                    continue;
                }
                if (data[i + 1] == '}') {
                    // Anonymous {} placeholder
                } else {
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] constexpr size_t countTotalPlaceholders() const noexcept
    {
        size_t count = 0;
        for (size_t i = 0; i + 1 < N; ++i) {
            if (data[i] == '{') {
                if (data[i + 1] == '{') {
                    ++i;
                    continue;
                }
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] constexpr std::string_view getPlaceholderName(size_t targetIdx) const noexcept
    {
        size_t currentIdx = 0;
        for (size_t i = 0; i + 1 < N; ++i) {
            if (data[i] == '{') {
                if (data[i + 1] == '{') {
                    ++i;
                    continue;
                }
                if (data[i + 1] == '}') {
                    if (currentIdx == targetIdx) {
                        return "";
                    }
                    ++currentIdx;
                    ++i;
                    continue;
                }
                const size_t start = i + 1;
                size_t end = start;
                while (end < N && data[end] != '}' && data[end] != ':') {
                    ++end;
                }
                if (currentIdx == targetIdx) {
                    return std::string_view(&data[start], end - start);
                }
                ++currentIdx;
                while (end < N && data[end] != '}') {
                    ++end;
                }
                i = end;
            }
        }
        return "";
    }

    [[nodiscard]] constexpr std::array<char, N> toAnonymous() const noexcept
    {
        std::array<char, N> result { };
        size_t w = 0;
        for (size_t r = 0; r < N && data[r] != '\0'; ++r) {
            if (data[r] == '{') {
                if (data[r + 1] == '{') {
                    result[w++] = '{';
                    result[w++] = '{';
                    ++r;
                    continue;
                }
                result[w++] = '{';
                size_t p = r + 1;
                while (p < N && data[p] != '\0' && data[p] != '}' && data[p] != ':') {
                    ++p;
                }
                if (p < N && data[p] == ':') {
                    while (p < N && data[p] != '\0' && data[p] != '}') {
                        result[w++] = data[p++];
                    }
                }
                result[w++] = '}';
                r = p;
            } else {
                result[w++] = data[r];
            }
        }
        result[w] = '\0';
        return result;
    }
};

template <typename T, typename = void>
struct FieldBuilder {
    static LogField build(std::string_view key, const T& val)
    {
        using CleanT = std::decay_t<T>;
        if constexpr (std::is_same_v<CleanT, LogField>) {
            return val;
        } else if constexpr (IsReflectedEntity<CleanT>) {
            return LogField::rawJson(key, serializeReflectedToJson(val));
        } else if constexpr (std::is_constructible_v<LogField, std::string_view, T>) {
            return LogField(key, val);
        } else {
            return LogField(key, fmt::format("{}", val));
        }
    }
};

template <typename T>
LogField makeField(std::string_view key, const T& val)
{
    return FieldBuilder<std::decay_t<T>>::build(key, val);
}

template <size_t N>
std::array<std::string_view, N> parseVariableNames(std::string_view str)
{
    std::array<std::string_view, N> result { };
    size_t start = 0;
    size_t arrIdx = 0;
    while (start < str.size() && arrIdx < N) {
        while (start < str.size() && (str[start] == ' ' || str[start] == ',')) {
            ++start;
        }
        size_t end = start;
        while (end < str.size() && str[end] != ',') {
            ++end;
        }
        size_t trimEnd = end;
        while (trimEnd > start && str[trimEnd - 1] == ' ') {
            --trimEnd;
        }
        if (start < trimEnd) {
            result[arrIdx++] = str.substr(start, trimEnd - start);
        }
        start = end + 1;
    }
    return result;
}

template <typename... Args>
void logNamed(Level level, std::string_view component, std::string_view fmtPattern,
    const std::array<std::string_view, sizeof...(Args)>& names, const Args&... args)
{
    if (!::Proxy::Log::shouldLog(level)) {
        return;
    }

    std::string formattedMsg = fmt::vformat(fmtPattern, fmt::make_format_args(args...));

    size_t idx = 0;
    std::array<LogField, sizeof...(Args)> fields { };
    (([&]() {
        fields[idx] = makeField(names[idx], args);
        ++idx;
    }()),
        ...);

    StructuredLogRecord record;
    record.level = level;
    record.component = component;
    record.message = formattedMsg;
    record.fields = fields.data();
    record.fieldCount = sizeof...(Args);
    record.isInterpolated = true;

    ::Proxy::Log::logStructured(record);
}

template <typename... Args>
void logPlain(Level level, std::string_view component, std::string_view message,
    std::string_view varNamesStr, const Args&... args)
{
    if (!::Proxy::Log::shouldLog(level)) {
        return;
    }

    constexpr size_t count = sizeof...(Args);
    std::array<std::string_view, count> names = parseVariableNames<count>(varNamesStr);
    std::array<LogField, count> fields { };

    size_t idx = 0;
    (([&]() {
        fields[idx] = makeField(names[idx], args);
        ++idx;
    }()),
        ...);

    StructuredLogRecord record;
    record.level = level;
    record.component = component;
    record.message = message;
    record.fields = fields.data();
    record.fieldCount = count;

    ::Proxy::Log::logStructured(record);
}

inline void logSimple(Level level, std::string_view component, std::string_view message)
{
    if (!::Proxy::Log::shouldLog(level)) {
        return;
    }
    StructuredLogRecord record;
    record.level = level;
    record.component = component;
    record.message = message;
    record.fields = nullptr;
    record.fieldCount = 0;
    ::Proxy::Log::logStructured(record);
}

template <const auto& RawFmt, typename... Args>
void dispatchFormat(Level level, std::string_view component, std::string_view varNamesStr, const Args&... args)
{
    constexpr size_t namedCount = RawFmt.countNamedPlaceholders();
    constexpr size_t totalCount = RawFmt.countTotalPlaceholders();

    if constexpr (namedCount == sizeof...(Args) && namedCount > 0) {
        // Case 1: All arguments matched to named placeholders {field}
        static constexpr auto anonChars = RawFmt.toAnonymous();
        constexpr auto names = []() {
            std::array<std::string_view, namedCount> arr {};
            for (size_t i = 0; i < namedCount; ++i) {
                arr[i] = RawFmt.getPlaceholderName(i);
            }
            return arr;
        }();
        logNamed(level, component, std::string_view(anonChars.data()), names, args...);
    } else if constexpr (totalCount == sizeof...(Args) && totalCount > 0) {
        // Case 2: Anonymous (or mixed) placeholders: interpolate sentence, and use expression names as fields
        std::array<std::string_view, sizeof...(Args)> names = parseVariableNames<sizeof...(Args)>(varNamesStr);
        logNamed(level, component, std::string_view(RawFmt.data), names, args...);
    } else {
        // Case 3: Plain message without placeholders; trailing arguments become fields
        logPlain(level, component, std::string_view(RawFmt.data), varNamesStr, args...);
    }
}

} // namespace Proxy::Log::Detail

#define PROXY_LOG_EXPAND_ARGS(level, fmtStr, ...)                                 \
    do {                                                                          \
        if (::Proxy::Log::shouldLog(level)) {                                     \
            static constexpr ::Proxy::Log::Detail::StaticString s_rawFmt(fmtStr); \
            ::Proxy::Log::Detail::dispatchFormat<s_rawFmt>(                       \
                level, PROXY_LOG_CURRENT_COMPONENT, #__VA_ARGS__, ##__VA_ARGS__); \
        }                                                                         \
    } while (0)

#define PROXY_LOG_DISPATCH_1(level, msg) \
    ::Proxy::Log::Detail::logSimple(level, PROXY_LOG_CURRENT_COMPONENT, msg)

#define PROXY_LOG_DISPATCH_N(level, msg, ...) \
    PROXY_LOG_EXPAND_ARGS(level, msg, __VA_ARGS__)

#define PROXY_LOG_ARG16(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, ...) _16

#define PROXY_LOG_HAS_COMMA(...) \
    PROXY_LOG_ARG16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)

#define PROXY_LOG_DISPATCH_CHOOSER(has_comma) \
    PROXY_LOG_DISPATCH_CHOOSER_IMPL(has_comma)

#define PROXY_LOG_DISPATCH_CHOOSER_IMPL(has_comma) \
    PROXY_LOG_DISPATCH_CHOOSER_##has_comma

#define PROXY_LOG_DISPATCH_CHOOSER_0(level, ...) \
    PROXY_LOG_DISPATCH_1(level, __VA_ARGS__)

#define PROXY_LOG_DISPATCH_CHOOSER_1(level, ...) \
    PROXY_LOG_DISPATCH_N(level, __VA_ARGS__)

#define PROXY_LOG_DISPATCH(level, ...) \
    PROXY_LOG_DISPATCH_CHOOSER(PROXY_LOG_HAS_COMMA(__VA_ARGS__))(level, __VA_ARGS__)

#define PROXY_LOG_TRACE(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Trace, __VA_ARGS__)
#define PROXY_LOG_DEBUG(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Debug, __VA_ARGS__)
#define PROXY_LOG_INFO(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Info, __VA_ARGS__)
#define PROXY_LOG_WARN(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Warn, __VA_ARGS__)
#define PROXY_LOG_ERROR(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Error, __VA_ARGS__)
#define PROXY_LOG_CRITICAL(...) PROXY_LOG_DISPATCH(::Proxy::Log::Level::Critical, __VA_ARGS__)
