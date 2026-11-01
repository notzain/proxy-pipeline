#pragma once

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES

#include "core/serialization/ir/Value.hpp"
#include "core/serialization/xml/XmlFormat.hpp"
#include "core/serialization/xml/XmlTraits.hpp"

#include <functional>
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
    using AttrPredicate = std::function<bool(std::string_view)>;

    /**
     * @brief Encodes an intermediate Value to an XML string for type T using Xerces-C DOM,
     * consulting XmlTraits<T> directly at compile-time.
     *
     * @tparam T The original type serialized into ir.
     * @param ir The intermediate Value to encode.
     * @param options Formatting options (root tag name, indentation, etc.).
     * @return Encoded XML string.
     */
    template <typename T>
    [[nodiscard]] static std::string encode(const Ir::Value& ir, const Options& options = { })
    {
        return encodeInternal(ir, options, [](std::string_view name) -> bool {
            using CleanT = Common::Compat::remove_cvref_t<T>;
            if constexpr (ReflectedType<CleanT>) {
                constexpr auto xmlConfig = XmlTraits<CleanT>::configure();
                constexpr auto fields = TypeTraits<CleanT>::fields();
                bool isAttr = false;
                std::apply([&](const auto&... fieldDesc) {
                    auto check = [&](const auto& desc) {
                        if (desc.name == name && xmlConfig.isAttribute(desc.memberPtr)) {
                            isAttr = true;
                        }
                    };
                    (check(fieldDesc), ...);
                },
                    fields);
                return isAttr;
            }
            return false;
        });
    }

    /**
     * @brief Fallback encode without type information (all fields become child elements).
     */
    [[nodiscard]] static std::string encode(const Ir::Value& ir, const Options& options = { })
    {
        return encodeInternal(ir, options, nullptr);
    }

    /**
     * @brief Decodes an XML string into an intermediate Value using Xerces-C DOM.
     *
     * @param input XML string or view.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Ir::Value> decode(std::string_view input, const Options& options = { });

    /**
     * @brief Decodes a mutable XML buffer in-place into an intermediate Value without copying.
     *
     * @param data Pointer to mutable XML buffer.
     * @param size Size of the buffer in bytes.
     * @param options Parsing options (root tag name, etc.).
     * @return Deserialized Value if parsing succeeds, std::nullopt otherwise.
     */
    [[nodiscard]] static std::optional<Ir::Value> decodeInPlace(void* data, std::size_t size, const Options& options = { });

private:
    [[nodiscard]] static std::string encodeInternal(const Ir::Value& ir, const Options& options, const AttrPredicate& isAttr);
};

} // namespace Core::Serialization::Xml

#endif
