#pragma once

#include "core/serialization/Converter.hpp"
#include "core/serialization/JsonFormat.hpp"
#include "core/serialization/Value.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization {

/**
 * @brief Serializes a value of type T into a JSON string using intermediate representation.
 *
 * @tparam T Type to serialize (POD with TypeTraits, container, optional, primitive).
 */
template <typename T>
class JsonSerializer {
public:
    using Options = JsonOptions;

    /**
     * @brief Formats the value into a JSON string.
     *
     * @param value The value to serialize.
     * @param options Formatting options (indentation, etc.).
     * @return std::string Encoded JSON string.
     */
    static std::string serialize(const T& value, const Options& options = { })
    {
        Value ir = toIntermediate(value);
        return JsonFormat::encode(ir, options);
    }

    /**
     * @brief Functor operator for convenient serializer instances.
     */
    std::string operator()(const T& value, const Options& options = { }) const
    {
        return serialize(value, options);
    }
};

/**
 * @brief Deserializes a JSON string into a value of type T using intermediate representation.
 *
 * @tparam T Target type to deserialize into.
 */
template <typename T>
class JsonDeserializer {
public:
    /**
     * @brief Parses a JSON string into the target type T.
     *
     * @param input JSON string or view.
     * @return std::optional<T> Deserialized object if parsing and mapping succeed, std::nullopt otherwise.
     */
    static std::optional<T> deserialize(std::string_view input)
    {
        auto ir = JsonFormat::decode(input);
        if (!ir) {
            return std::nullopt;
        }
        return fromIntermediate<T>(*ir);
    }

    /**
     * @brief Functor operator for convenient deserializer instances.
     */
    std::optional<T> operator()(std::string_view input) const
    {
        return deserialize(input);
    }
};

} // namespace Core::Serialization
