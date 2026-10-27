#pragma once

#include "core/serialization/ir/Converter.hpp"
#include "core/serialization/ir/Value.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization {

namespace detail {

    template <typename Format, typename Input, typename Options, typename = void>
    struct HasDecodeWithOptions : std::false_type { };

    template <typename Format, typename Input, typename Options>
    struct HasDecodeWithOptions<Format, Input, Options,
        std::void_t<decltype(Format::decode(std::declval<Input>(), std::declval<Options>()))>> : std::true_type { };

} // namespace detail

/**
 * @brief Serializes any supported type to the specified wire format.
 *
 * @tparam Format Format policy (e.g. JsonFormat, XmlFormat).
 * @tparam T Type to serialize (POD with TypeTraits, std container, optional, primitive).
 * @param value The value to serialize.
 * @param options Format-specific options.
 * @return std::string Serialized string representation.
 */
template <typename Format, typename T>
std::string serialize(const T& value, const typename Format::Options& options = { })
{
    Ir::Value ir = Ir::toIntermediate(value);
    return Format::encode(ir, options);
}

/**
 * @brief Deserializes from the specified wire format into the requested type.
 *
 * @tparam Format Format policy (e.g. JsonFormat, XmlFormat).
 * @tparam T Desired output type.
 * @param input Wire format string.
 * @return std::optional<T> Deserialized value if successful, std::nullopt otherwise.
 */
template <typename Format, typename T>
std::optional<T> deserialize(std::string_view input, const typename Format::Options& options = { })
{
    std::optional<Ir::Value> ir;
    if constexpr (detail::HasDecodeWithOptions<Format, std::string_view, typename Format::Options>::value) {
        ir = Format::decode(input, options);
    } else {
        ir = Format::decode(input);
    }
    if (!ir) {
        return std::nullopt;
    }
    return Ir::fromIntermediate<T>(*ir);
}

} // namespace Core::Serialization
