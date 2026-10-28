#pragma once

#include "backend/EnvelopeHeader.hpp"
#include "backend/MessageView.hpp"
#include "backend/ProtocolId.hpp"
#include "backend/UdpMetadata.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Backend {

/**
 * @brief Factory and serializer for constructing valid binary Envelope buffers.
 */
class MessageEnvelope {
public:
    /**
     * @brief Constructs a serialized envelope containing metadata and application payload.
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
     *
     * @param view Source MessageView providing original header and metadata.
     * @param newPayload Mutated application payload.
     * @return std::vector<std::byte> Serialized binary envelope.
     */
    static std::vector<std::byte> withPayload(
        const MessageView& view,
        Common::Compat::span<const std::byte> newPayload);
};

} // namespace Backend
