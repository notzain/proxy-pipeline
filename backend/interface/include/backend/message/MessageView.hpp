#pragma once

#include "backend/memory/Lease.hpp"
#include "backend/message/MetadataTraits.hpp"
#include "backend/message/ProtocolId.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/Borrow.hpp"
#include "common/memory/ObserverPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <vector>

namespace Backend {

/**
 * @brief Zero-copy, non-owning view representing an in-flight message.
 *
 * Exposes the application payload as its primary field to users, backends, and handlers.
 * Can be constructed from native Proxy envelope wire buffers or directly by
 * custom backend transports and codecs. Encapsulates an optional transport Lease so
 * output channels can consume and forward the underlying message object transparently.
 *
 * Also provides protocol-agnostic static wire format packing helpers (pack, packRaw, packInto).
 */
class MessageView {
public:
    /**
     * @brief Parses and validates an envelope from a raw Proxy binary envelope buffer.
     *
     * @param buffer Non-owning view of raw bytes received from an IPC channel.
     * @return std::optional<MessageView> Valid view if the header and lengths match, or std::nullopt.
     */
    static std::optional<MessageView> from(Common::Compat::span<const std::byte> buffer) noexcept;

    /**
     * @brief Constructs a serialized binary envelope containing metadata and application payload.
     *
     * @param protocol Transport protocol identifier.
     * @param metadata Non-owning view of metadata bytes.
     * @param payload Non-owning view of payload bytes.
     * @param timestampNs Ingress timestamp in nanoseconds (0 to use current steady/system clock).
     * @return std::vector<std::byte> Serialized binary envelope.
     */
    static std::vector<std::byte> pack(
        ProtocolId protocol,
        Common::Compat::span<const std::byte> metadata,
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Packs a strongly-typed protocol metadata struct `T` (registered via `MetadataTraits<T>`)
     *        together with an application payload.
     */
    template <typename T>
    static std::vector<std::byte> pack(
        const T& metadata,
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0)
    {
        static_assert(MetadataTraits<T>::isRegistered, "Metadata type T must specialize Backend::MetadataTraits<T>");
        static_assert(std::is_trivially_copyable_v<T>, "Metadata type T must be trivially copyable");
        return pack(
            MetadataTraits<T>::protocol,
            Common::Compat::span<const std::byte>(reinterpret_cast<const std::byte*>(&metadata), sizeof(T)),
            payload,
            timestampNs);
    }

    /**
     * @brief Packs an unadorned/raw application payload (RawProtocol, empty metadata).
     */
    static std::vector<std::byte> packRaw(
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Serializes an envelope directly into a pre-allocated buffer without heap allocation.
     *
     * @param destination Target memory span. Must be at least sizeof(EnvelopeHeader) + meta.size() + payload.size().
     * @param protocol Transport protocol identifier.
     * @param metadata Metadata byte view.
     * @param payload Payload byte view.
     * @param timestampNs Timestamp in nanoseconds.
     * @return std::size_t Number of bytes written into destination, or 0 if destination is too small.
     */
    static std::size_t packInto(
        Common::Compat::span<std::byte> destination,
        ProtocolId protocol,
        Common::Compat::span<const std::byte> metadata,
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Serializes a strongly-typed metadata struct `T` and payload directly into a pre-allocated buffer.
     */
    template <typename T>
    static std::size_t packInto(
        Common::Compat::span<std::byte> destination,
        const T& metadata,
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0)
    {
        static_assert(MetadataTraits<T>::isRegistered, "Metadata type T must specialize Backend::MetadataTraits<T>");
        static_assert(std::is_trivially_copyable_v<T>, "Metadata type T must be trivially copyable");
        return packInto(
            destination,
            MetadataTraits<T>::protocol,
            Common::Compat::span<const std::byte>(reinterpret_cast<const std::byte*>(&metadata), sizeof(T)),
            payload,
            timestampNs);
    }

    /**
     * @brief Constructs a new serialized binary envelope reusing @p view's metadata and timestamp with @p newPayload.
     */
    static std::vector<std::byte> withPayload(
        const MessageView& view,
        Common::Compat::span<const std::byte> newPayload);

    /**
     * @brief Returns a non-allocating MessageView referencing @p newPayload while preserving this message's
     *        protocol, metadata, timestamp, and attached Lease.
     *
     * When passed to `IOutputChannel::send(const MessageView&)`, buffer-based transports pack the preserved
     * metadata and new payload into an envelope, while SDK transports (e.g. `VendorOutputChannel`) transmit
     * the updated payload directly.
     */
    [[nodiscard]] MessageView withPayload(Common::Compat::span<const std::byte> newPayload) const noexcept;

    /**
     * @brief Returns true if `rawBuffer()` contains a complete pre-packed wire envelope matching `payload()`.
     */
    [[nodiscard]] bool hasWireEnvelope() const noexcept;

    constexpr MessageView() noexcept = default;

    /**
     * @brief Constructs a view from explicit payload and optional raw buffer, timestamp, and metadata.
     */
    explicit MessageView(
        Common::Compat::span<const std::byte> payload,
        Common::Compat::span<const std::byte> rawBuffer = { },
        std::uint64_t timestampNs = 0,
        ProtocolId protocol = RawProtocol,
        Common::Compat::span<const std::byte> metadata = { }) noexcept;

    MessageView(
        Common::Compat::span<const std::byte> rawBuffer,
        Common::Compat::span<const std::byte> metadata,
        Common::Compat::span<const std::byte> payload,
        ProtocolId protocol,
        std::uint64_t timestampNs) noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> payload() const noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> rawBuffer() const noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> metadata() const noexcept;

    [[nodiscard]] ProtocolId protocol() const noexcept;

    [[nodiscard]] std::uint64_t timestampNs() const noexcept;

    /**
     * @brief Binds the in-flight transport lease associated with this message view.
     */
    void attachLease(Common::Memory::Borrow<Lease> lease) noexcept;

    /**
     * @brief Returns a non-owning observer pointer to the attached SettlementToken, if present.
     */
    [[nodiscard]] Common::Memory::ObserverPtr<SettlementToken> token() const noexcept;

    /**
     * @brief Deserializes protocol metadata `T` safely by value without unaligned pointer casts.
     *
     * Verifies both the protocol identifier via `MetadataTraits<T>` and the metadata size.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> getMetadata() const noexcept
    {
        static_assert(MetadataTraits<T>::isRegistered, "Metadata type T must specialize Backend::MetadataTraits<T>");
        static_assert(std::is_trivially_copyable_v<T>, "Metadata type T must be trivially copyable");
        if (m_protocol != MetadataTraits<T>::protocol || m_metadata.size() < sizeof(T)) {
            return std::nullopt;
        }
        T out { };
        std::memcpy(&out, m_metadata.data(), sizeof(T));
        return out;
    }

private:
    Common::Compat::span<const std::byte> m_rawBuffer { };
    Common::Compat::span<const std::byte> m_metadata { };
    Common::Compat::span<const std::byte> m_payload { };
    std::uint64_t m_timestampNs { 0 };
    ProtocolId m_protocol { RawProtocol };
    Common::Memory::ObserverPtr<Lease> m_lease { };
};

} // namespace Backend
