#pragma once

#include "core/message/EnvelopeHeader.hpp"
#include "core/message/MessageView.hpp"
#include "core/message/ProtocolId.hpp"
#include "core/message/UdpMetadata.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Core::Message {

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
        std::span<const std::byte> metadata,
        std::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Packs an unadorned/raw application payload (ProtocolId::Raw, empty metadata).
     */
    static std::vector<std::byte> packRaw(
        std::span<const std::byte> payload,
        std::uint64_t timestampNs = 0);

    /**
     * @brief Packs a UDP datagram payload along with origin/destination UdpMetadata.
     */
    static std::vector<std::byte> packUdp(
        const UdpMetadata& meta,
        std::span<const std::byte> payload,
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
        std::span<std::byte> destination,
        ProtocolId protocol,
        std::span<const std::byte> metadata,
        std::span<const std::byte> payload,
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
        std::span<const std::byte> newPayload);
};

} // namespace Core::Message
