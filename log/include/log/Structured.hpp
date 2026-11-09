#pragma once

#include "log/ILogSink.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Proxy::Log {

/**
 * @brief Represents a single key-value structured field attached to a log record.
 */
class LogField {
public:
    using ValueType = std::variant<
        std::monostate,
        bool,
        int64_t,
        uint64_t,
        double,
        std::string_view,
        std::string>;

    constexpr LogField() noexcept
        : m_key("")
        , m_value(std::monostate { })
    {
    }

    constexpr LogField(std::string_view key, bool val) noexcept
        : m_key(key)
        , m_value(val)
    {
    }

    constexpr LogField(std::string_view key, int val) noexcept
        : m_key(key)
        , m_value(static_cast<int64_t>(val))
    {
    }

    constexpr LogField(std::string_view key, int64_t val) noexcept
        : m_key(key)
        , m_value(val)
    {
    }

    constexpr LogField(std::string_view key, unsigned int val) noexcept
        : m_key(key)
        , m_value(static_cast<uint64_t>(val))
    {
    }

    constexpr LogField(std::string_view key, uint64_t val) noexcept
        : m_key(key)
        , m_value(val)
    {
    }

    constexpr LogField(std::string_view key, double val) noexcept
        : m_key(key)
        , m_value(val)
    {
    }

    constexpr LogField(std::string_view key, std::string_view val) noexcept
        : m_key(key)
        , m_value(val)
    {
    }

    LogField(std::string_view key, std::string val) noexcept
        : m_key(key)
        , m_value(std::move(val))
    {
    }

    LogField(std::string_view key, const char* val) noexcept
        : m_key(key)
        , m_value(std::string_view(val ? val : ""))
    {
    }

    [[nodiscard]] constexpr std::string_view key() const noexcept { return m_key; }
    [[nodiscard]] const ValueType& value() const noexcept { return m_value; }

private:
    std::string_view m_key;
    ValueType m_value;
};

/**
 * @brief Represents a fully structured log event ready for emission.
 */
struct StructuredLogRecord {
    Level level { Level::Info };
    std::string_view component { "" };
    std::string_view message { "" };
    const LogField* fields { nullptr };
    size_t fieldCount { 0 };
};

} // namespace Proxy::Log
