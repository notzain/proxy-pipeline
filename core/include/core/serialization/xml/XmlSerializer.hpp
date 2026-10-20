#pragma once

#include "core/serialization/ir/Converter.hpp"
#include "core/serialization/ir/Value.hpp"
#include "core/serialization/xml/XmlFormat.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization::Xml {

/**
 * @brief Serializes a value of type T into an XML string using intermediate representation.
 *
 * @tparam T Type to serialize (POD with TypeTraits, container, optional, primitive).
 */
template <typename T>
class XmlSerializer {
public:
    using Options = XmlOptions;

    /**
     * @brief Formats the value into an XML string.
     *
     * @param value The value to serialize.
     * @param options Formatting options (root tag name, indentation, etc.).
     * @return std::string Encoded XML string.
     */
    static std::string serialize(const T& value, const Options& options = { })
    {
        Ir::Value ir = Ir::toIntermediate(value);
        return XmlFormat::encode(ir, options);
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
 * @brief Deserializes an XML string into a value of type T using intermediate representation.
 *
 * @tparam T Target type to deserialize into.
 */
template <typename T>
class XmlDeserializer {
public:
    using Options = XmlOptions;

    /**
     * @brief Parses an XML string into the target type T.
     *
     * @param input XML string or view.
     * @param options Parsing options (root tag name, etc.).
     * @return std::optional<T> Deserialized object if parsing and mapping succeed, std::nullopt otherwise.
     */
    static std::optional<T> deserialize(std::string_view input, const Options& options = { })
    {
        auto ir = XmlFormat::decode(input, options);
        if (!ir) {
            return std::nullopt;
        }
        return Ir::fromIntermediate<T>(*ir);
    }

    /**
     * @brief Parses a mutable XML buffer in-place into the target type T without copying.
     *
     * @param data Pointer to mutable XML buffer.
     * @param size Size in bytes.
     * @param options Parsing options (root tag name, etc.).
     * @return std::optional<T> Deserialized object if parsing and mapping succeed, std::nullopt otherwise.
     */
    static std::optional<T> deserializeInPlace(void* data, std::size_t size, const Options& options = { })
    {
        auto ir = XmlFormat::decodeInPlace(data, size, options);
        if (!ir) {
            return std::nullopt;
        }
        return Ir::fromIntermediate<T>(*ir);
    }

    /**
     * @brief Functor operator for convenient deserializer instances.
     */
    std::optional<T> operator()(std::string_view input, const Options& options = { }) const
    {
        return deserialize(input, options);
    }
};

} // namespace Core::Serialization::Xml
