#pragma once

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES

#include "core/serialization/ir/Value.hpp"
#include "core/serialization/xml/XmlFormat.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Core::Serialization::Xml {

/**
 * @brief XML serialization format policy using Apache Xerces-C.
 *
 * Exposes methods to encode intermediate Value trees into XML strings,
 * and decode XML strings or buffers into intermediate Value trees.
 */
class XercesFormat {
public:
    using Options = XmlOptions;

    /**
     * @brief Encodes an intermediate Value to an XML string using Xerces-C DOM.
     *
     * @param ir The intermediate Value to encode.
     * @param options Formatting options (root tag name, indentation, etc.).
     * @return Encoded XML string.
     */
    [[nodiscard]] static std::string encode(const Ir::Value& ir, const Options& options = {});

    /**
     * @brief Decodes an XML string into an intermediate Value using Xerces-C DOM.
     *
     * @param input XML string or view.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Ir::Value> decode(std::string_view input, const Options& options = {});

    /**
     * @brief Decodes a mutable XML buffer in-place into an intermediate Value without copying.
     *
     * @param data Pointer to mutable XML buffer.
     * @param size Size of the buffer in bytes.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Ir::Value> decodeInPlace(void* data, std::size_t size, const Options& options = {});
};

} // namespace Core::Serialization::Xml

#endif // PROXY_HAS_XERCES
