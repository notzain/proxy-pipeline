#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Core::Serialization {

class Value;

using NullType = std::monostate;
using ArrayType = std::vector<Value>;
using ObjectType = std::map<std::string, Value>;
using AttributeMapType = std::map<std::string, std::string>;

/**
 * @brief Intermediate Representation (IR) value node representing dynamically-typed data.
 */
class Value {
public:
    using VariantType = std::variant<
        NullType,
        bool,
        std::int64_t,
        std::uint64_t,
        double,
        std::string,
        std::string_view,
        ArrayType,
        ObjectType>;

    Value()
        : m_data(NullType { })
    {
    }

    Value(NullType)
        : m_data(NullType { })
    {
    }

    Value(bool v)
        : m_data(v)
    {
    }

    Value(int v)
        : m_data(static_cast<std::int64_t>(v))
    {
    }

    Value(long v)
        : m_data(static_cast<std::int64_t>(v))
    {
    }

    Value(long long v)
        : m_data(static_cast<std::int64_t>(v))
    {
    }

    Value(unsigned int v)
        : m_data(static_cast<std::uint64_t>(v))
    {
    }

    Value(unsigned long v)
        : m_data(static_cast<std::uint64_t>(v))
    {
    }

    Value(unsigned long long v)
        : m_data(static_cast<std::uint64_t>(v))
    {
    }

    Value(float v)
        : m_data(static_cast<double>(v))
    {
    }

    Value(double v)
        : m_data(v)
    {
    }

    Value(std::string v)
        : m_data(std::move(v))
    {
    }

    Value(std::string_view v)
        : m_data(v)
    {
    }

    Value(const char* v)
        : m_data(v ? std::string_view(v) : std::string_view(""))
    {
    }

    Value(ArrayType v)
        : m_data(std::move(v))
    {
    }

    Value(ObjectType v)
        : m_data(std::move(v))
    {
    }

    [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<NullType>(m_data); }
    [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(m_data); }
    [[nodiscard]] bool isInt() const noexcept { return std::holds_alternative<std::int64_t>(m_data); }
    [[nodiscard]] bool isUInt() const noexcept { return std::holds_alternative<std::uint64_t>(m_data); }
    [[nodiscard]] bool isDouble() const noexcept { return std::holds_alternative<double>(m_data); }
    [[nodiscard]] bool isNumber() const noexcept { return isInt() || isUInt() || isDouble(); }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(m_data) || std::holds_alternative<std::string_view>(m_data); }
    [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<ArrayType>(m_data); }
    [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<ObjectType>(m_data); }

    [[nodiscard]] const VariantType& raw() const noexcept { return m_data; }
    [[nodiscard]] VariantType& raw() noexcept { return m_data; }

    [[nodiscard]] std::optional<bool> asBool() const noexcept
    {
        if (auto* val = std::get_if<bool>(&m_data)) {
            return *val;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> asInt() const noexcept
    {
        if (auto* val = std::get_if<std::int64_t>(&m_data)) {
            return *val;
        }
        if (auto* val = std::get_if<std::uint64_t>(&m_data)) {
            return static_cast<std::int64_t>(*val);
        }
        if (auto* val = std::get_if<double>(&m_data)) {
            return static_cast<std::int64_t>(*val);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> asUInt() const noexcept
    {
        if (auto* val = std::get_if<std::uint64_t>(&m_data)) {
            return *val;
        }
        if (auto* val = std::get_if<std::int64_t>(&m_data); val && *val >= 0) {
            return static_cast<std::uint64_t>(*val);
        }
        if (auto* val = std::get_if<double>(&m_data); val && *val >= 0) {
            return static_cast<std::uint64_t>(*val);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> asDouble() const noexcept
    {
        if (auto* val = std::get_if<double>(&m_data)) {
            return *val;
        }
        if (auto* val = std::get_if<std::int64_t>(&m_data)) {
            return static_cast<double>(*val);
        }
        if (auto* val = std::get_if<std::uint64_t>(&m_data)) {
            return static_cast<double>(*val);
        }
        return std::nullopt;
    }

    [[nodiscard]] const std::string* asString() const noexcept
    {
        return std::get_if<std::string>(&m_data);
    }

    [[nodiscard]] std::optional<std::string_view> asStringView() const noexcept
    {
        if (auto* sv = std::get_if<std::string_view>(&m_data)) {
            return *sv;
        }
        if (auto* s = std::get_if<std::string>(&m_data)) {
            return std::string_view(*s);
        }
        return std::nullopt;
    }

    [[nodiscard]] const ArrayType* asArray() const noexcept
    {
        return std::get_if<ArrayType>(&m_data);
    }

    [[nodiscard]] ArrayType* asArray() noexcept
    {
        return std::get_if<ArrayType>(&m_data);
    }

    [[nodiscard]] const ObjectType* asObject() const noexcept
    {
        return std::get_if<ObjectType>(&m_data);
    }

    [[nodiscard]] ObjectType* asObject() noexcept
    {
        return std::get_if<ObjectType>(&m_data);
    }

    [[nodiscard]] const Value* findField(std::string_view key) const
    {
        if (auto* obj = asObject()) {
            if (auto it = obj->find(std::string(key)); it != obj->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    void setField(std::string key, Value val)
    {
        if (!isObject()) {
            m_data = ObjectType { };
        }
        std::get<ObjectType>(m_data)[std::move(key)] = std::move(val);
    }

    void pushBack(Value val)
    {
        if (!isArray()) {
            m_data = ArrayType { };
        }
        std::get<ArrayType>(m_data).push_back(std::move(val));
    }

    // --- Attributes support (for XML attributes) ---
    [[nodiscard]] const AttributeMapType& attributes() const noexcept
    {
        return m_attributes;
    }

    [[nodiscard]] AttributeMapType& attributes() noexcept
    {
        return m_attributes;
    }

    [[nodiscard]] const std::string* findAttribute(std::string_view key) const
    {
        if (auto it = m_attributes.find(std::string(key)); it != m_attributes.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void setAttribute(std::string key, std::string value)
    {
        m_attributes[std::move(key)] = std::move(value);
    }

private:
    VariantType m_data;
    AttributeMapType m_attributes;
};

} // namespace Core::Serialization
