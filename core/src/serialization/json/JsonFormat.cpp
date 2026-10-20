#include "core/serialization/json/JsonFormat.hpp"

#include <nlohmann/json.hpp>

namespace Core::Serialization::Json {

namespace {

    nlohmann::json toNlohmann(const Ir::Value& val)
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

    Ir::Value fromNlohmann(const nlohmann::json& j)
    {
        if (j.is_null()) {
            return Ir::Value(Ir::NullType { });
        }
        if (j.is_boolean()) {
            return Ir::Value(j.get<bool>());
        }
        if (j.is_number_unsigned()) {
            return Ir::Value(j.get<std::uint64_t>());
        }
        if (j.is_number_integer()) {
            return Ir::Value(j.get<std::int64_t>());
        }
        if (j.is_number_float()) {
            return Ir::Value(j.get<double>());
        }
        if (j.is_string()) {
            return Ir::Value(j.get<std::string>());
        }
        if (j.is_array()) {
            Ir::ArrayType arr;
            arr.reserve(j.size());
            for (const auto& item : j) {
                arr.push_back(fromNlohmann(item));
            }
            return Ir::Value(std::move(arr));
        }
        if (j.is_object()) {
            Ir::ObjectType obj;
            for (auto it = j.begin(); it != j.end(); ++it) {
                obj[it.key()] = fromNlohmann(it.value());
            }
            return Ir::Value(std::move(obj));
        }
        return Ir::Value(Ir::NullType { });
    }

} // namespace

std::string JsonFormat::encode(const Ir::Value& ir, const Options& options)
{
    nlohmann::json j = toNlohmann(ir);
    if (options.indent >= 0) {
        return j.dump(options.indent);
    }
    return j.dump();
}

std::optional<Ir::Value> JsonFormat::decode(std::string_view input)
{
    try {
        auto j = nlohmann::json::parse(input);
        return fromNlohmann(j);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace Core::Serialization::Json
