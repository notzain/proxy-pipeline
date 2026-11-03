#pragma once

#include "backend/message/MetadataTraits.hpp"
#include "backend/message/ProtocolId.hpp"
#include "common/compat/Span.hpp"
#include "core/serialization/TypeTraits.hpp"
#include "core/serialization/ir/Converter.hpp"
#include "core/serialization/ir/Value.hpp"
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Core::Metadata {

/**
 * @brief Decentralized reflection catalog for protocol metadata structures.
 *
 * Maps ProtocolId to an intermediate representation (IR) converter function without
 * depending on Lua or any external scripting library. Allows handlers (and LuaCondition)
 * to dynamically reflect fields from arbitrary metadata structures at runtime.
 */
class MetadataCatalog {
public:
    using ToIrFn = std::function<std::optional<Serialization::Ir::Value>(Common::Compat::span<const std::byte>)>;
    using FieldExtractorFn = std::function<std::optional<Serialization::Ir::Value>(Common::Compat::span<const std::byte>, std::string_view)>;

    /**
     * @brief Registers a strongly-typed metadata struct `T` into the catalog instance.
     */
    template <typename T>
    void registerTypeInstance()
    {
        static_assert(Backend::MetadataTraits<T>::isRegistered, "Metadata type T must specialize Backend::MetadataTraits<T>");
        static_assert(Serialization::TypeTraits<T>::isReflected, "Metadata type T must specialize Core::Serialization::TypeTraits<T>");

        registerFn(Backend::MetadataTraits<T>::protocol, [](Common::Compat::span<const std::byte> bytes) -> std::optional<Serialization::Ir::Value> {
            if (bytes.size() < sizeof(T)) {
                return std::nullopt;
            }
            T obj { };
            std::memcpy(&obj, bytes.data(), sizeof(T));
            return Serialization::Ir::toIntermediate(obj);
        });

        registerFieldExtractorFn(Backend::MetadataTraits<T>::protocol, [](Common::Compat::span<const std::byte> bytes, std::string_view fieldName) -> std::optional<Serialization::Ir::Value> {
            if (bytes.size() < sizeof(T)) {
                return std::nullopt;
            }
            T obj { };
            std::memcpy(&obj, bytes.data(), sizeof(T));
            constexpr auto fieldTuple = Serialization::TypeTraits<T>::fields();

            std::optional<Serialization::Ir::Value> result = std::nullopt;
            std::apply([&](const auto&... desc) {
                auto extractOne = [&](const auto& d) {
                    if (result.has_value()) {
                        return;
                    }
                    if (d.name == fieldName) {
                        result = Serialization::Ir::toIntermediate(obj.*(d.memberPtr));
                    }
                };
                (extractOne(desc), ...);
            },
                fieldTuple);

            return result;
        });
    }

    /**
     * @brief Static helper to register a strongly-typed metadata struct `T`.
     */
    template <typename T>
    static void registerType()
    {
        instance().registerTypeInstance<T>();
    }

    /**
     * @brief Converts raw metadata bytes for a given protocol into dynamic IR value.
     */
    static std::optional<Serialization::Ir::Value> toIr(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes);

    /**
     * @brief Extracts a single field from metadata bytes by field name.
     */
    static std::optional<Serialization::Ir::Value> getField(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes, std::string_view fieldName);

    /**
     * @brief Explicitly registers an IR converter function for a protocol ID.
     */
    void registerFn(Backend::ProtocolId proto, ToIrFn fn);

    /**
     * @brief Explicitly registers a field extractor function for a protocol ID.
     */
    void registerFieldExtractorFn(Backend::ProtocolId proto, FieldExtractorFn fn);

    /**
     * @brief Formats metadata bytes into a human-readable JSON string.
     */
    static std::string formatJson(Backend::ProtocolId proto, Common::Compat::span<const std::byte> bytes, bool pretty = false);

    /**
     * @brief Returns singleton instance of the catalog.
     */
    static MetadataCatalog& instance();

private:
    MetadataCatalog();
    std::unordered_map<Backend::ProtocolId, ToIrFn> m_converters;
    std::unordered_map<Backend::ProtocolId, FieldExtractorFn> m_fieldExtractors;
};

} // namespace Core::Metadata

/**
 * @brief Decentralized auto-registration macro for metadata structs.
 *
 * Place in any compilation unit (.cpp) where a metadata struct and its TypeTraits are defined.
 */
#define PROXY_REGISTER_METADATA(Type)                                \
    namespace {                                                      \
        const bool _proxy_reg_val_##__LINE__ = []() {                \
            ::Core::Metadata::MetadataCatalog::registerType<Type>(); \
            return true;                                             \
        }();                                                         \
    }
