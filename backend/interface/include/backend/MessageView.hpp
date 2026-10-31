#pragma once

#include "backend/EnvelopeHeader.hpp"
#include "backend/Lease.hpp"
#include "backend/ProtocolId.hpp"
#include "backend/UdpMetadata.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/Borrow.hpp"
#include "common/memory/ObserverPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
 * Also provides static wire format packing helpers (pack, packRaw, packUdp, packInto).
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
     * @brief Packs an unadorned/raw application payload (ProtocolId::Raw, empty metadata).
     */
    static std::vector<std::byte> packRaw(
        Common::Compat::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Packs a UDP datagram payload along with origin/destination UdpMetadata.
     */
    static std::vector<std::byte> packUdp(
        const UdpMetadata& meta,
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
     * @brief Constructs a new envelope reusing the existing message's metadata but with a modified payload.
     */
    static std::vector<std::byte> withPayload(
        const MessageView& view,
        Common::Compat::span<const std::byte> newPayload);

    constexpr MessageView() noexcept = default;

    /**
     * @brief Constructs a view from explicit payload and optional raw buffer, timestamp, and metadata.
     */
    explicit MessageView(
        Common::Compat::span<const std::byte> payload,
        Common::Compat::span<const std::byte> rawBuffer = { },
        std::uint64_t timestampNs = 0,
        ProtocolId protocol = ProtocolId::Raw,
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
     * @brief Consumes and extracts the underlying transport lease state, if still active.
     *
     * Invoked by output channels that forward the actual transport message object (consuming it).
     * Subsequent calls on the same MessageView return nullptr.
     */
    [[nodiscard]] std::unique_ptr<ILeaseState> consumeLeaseState() const noexcept;

    /**
     * @brief Returns a pointer to the attached SettlementToken, if present.
     */
    [[nodiscard]] SettlementToken* token() const noexcept;

    /**
     * @brief Returns a non-owning observer pointer to the attached lease state, or nullptr if none.
     */
    [[nodiscard]] Common::Memory::ObserverPtr<ILeaseState> leaseState() const noexcept;

    template <typename T>
    [[nodiscard]] const T* getMetadata() const noexcept
    {
        if (m_metadata.size() < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(m_metadata.data());
    }

private:
    Common::Compat::span<const std::byte> m_rawBuffer { };
    Common::Compat::span<const std::byte> m_metadata { };
    Common::Compat::span<const std::byte> m_payload { };
    std::uint64_t m_timestampNs { 0 };
    ProtocolId m_protocol { ProtocolId::Raw };
    Common::Memory::ObserverPtr<Lease> m_lease { };
};

} // namespace Backend
