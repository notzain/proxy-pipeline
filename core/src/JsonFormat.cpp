#include "core/serialization/JsonFormat.hpp"

#include <nlohmann/json.hpp>

namespace Core::Serialization {

namespace {

    nlohmann::json toNlohmann(const Value& val)
    {
        if (val.isNull()) {
            return nullptr;
        }
        if (auto b = val.asBool()) {
            return *b;
        }
        if (val.isInt()) {
            return *val.asInt();
        }
        if (val.isUInt()) {
            return *val.asUInt();
        }
        if (val.isDouble()) {
            return *val.asDouble();
        }
        if (auto s = val.asStringView()) {
            return std::string(*s);
        }
        if (auto* arr = val.asArray()) {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& item : *arr) {
                j.push_back(toNlohmann(item));
            }
            return j;
        }
        if (auto* obj = val.asObject()) {
            nlohmann::json j = nlohmann::json::object();
            for (const auto& [k, v] : *obj) {
                j[k] = toNlohmann(v);
            }
            return j;
        }
        return nullptr;
    }

    Value fromNlohmann(const nlohmann::json& j)
    {
        if (j.is_null()) {
            return Value(NullType { });
        }
        if (j.is_boolean()) {
            return Value(j.get<bool>());
        }
        if (j.is_number_unsigned()) {
            return Value(j.get<std::uint64_t>());
        }
        if (j.is_number_integer()) {
            return Value(j.get<std::int64_t>());
        }
        if (j.is_number_float()) {
            return Value(j.get<double>());
        }
        if (j.is_string()) {
            return Value(j.get<std::string>());
        }
        if (j.is_array()) {
            ArrayType arr;
            arr.reserve(j.size());
            for (const auto& item : j) {
                arr.push_back(fromNlohmann(item));
            }
            return Value(std::move(arr));
        }
        if (j.is_object()) {
            ObjectType obj;
            for (auto it = j.begin(); it != j.end(); ++it) {
                obj[it.key()] = fromNlohmann(it.value());
            }
            return Value(std::move(obj));
        }
        return Value(NullType { });
    }

} // namespace

std::string JsonFormat::encode(const Value& ir, const Options& options)
{
    nlohmann::json j = toNlohmann(ir);
    if (options.indent >= 0) {
        return j.dump(options.indent);
    }
    return j.dump();
}

std::optional<Value> JsonFormat::decode(std::string_view input)
{
    try {
        auto j = nlohmann::json::parse(input);
        return fromNlohmann(j);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace Core::Serialization
