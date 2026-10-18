#pragma once

#include "core/serialization/Value.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization {

struct XmlOptions {
    std::string rootName = "root";
    int indentSpaces = 2;
};

/**
 * @brief XML serialization format policy using pugixml.
 *
 * Exposes methods to encode intermediate Value trees into XML strings,
 * and decode XML strings or in-place buffers into intermediate Value trees.
 */
class XmlFormat {
public:
    using Options = XmlOptions;

    /**
     * @brief Encodes an intermediate Value to an XML string.
     *
     * @param ir The intermediate Value to encode.
     * @param options Formatting options (root tag name, indentation, etc.).
     * @return Encoded XML string.
     */
    [[nodiscard]] static std::string encode(const Value& ir, const Options& options = { });

    /**
     * @brief Decodes an XML string into an intermediate Value.
     *
     * @param input XML string or view.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Value> decode(std::string_view input, const Options& options = { });

    /**
     * @brief Decodes a mutable XML buffer in-place into an intermediate Value without copying.
     *
     * @param data Pointer to mutable XML buffer.
     * @param size Size of the buffer in bytes.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Value> decodeInPlace(void* data, std::size_t size, const Options& options = { });
};

} // namespace Core::Serialization
