#pragma once

#include "backend/message/MetadataTraits.hpp"
#include "core/serialization/TypeTraits.hpp"
#include <cstdint>
#include <netinet/in.h>
#include <tuple>

namespace Core::Net {

/**
 * @brief Network transport metadata captured by UDP Ingress and restored at UDP Egress.
 *
 * All IP and port numbers are stored in network byte order (big-endian).
 */
struct alignas(4) UdpMetadata {
    std::uint32_t srcIp { 0 }; ///< Originating client IPv4 address (network byte order)
    std::uint16_t srcPort { 0 }; ///< Originating client UDP port (network byte order)
    std::uint16_t dstPort { 0 }; ///< Target / ingress UDP port (network byte order)
    std::uint32_t dstIp { 0 }; ///< Target / ingress IPv4 address (network byte order)
    std::uint8_t srcIpv6[16] { 0 }; ///< Originating client IPv6 address (when addressFamily == 6)
    std::uint8_t dstIpv6[16] { 0 }; ///< Target / ingress IPv6 address (when addressFamily == 6)
    std::uint8_t addressFamily { 4 }; ///< 4 for IPv4 (default), 6 for IPv6
    std::uint8_t reserved[3] { 0 }; ///< Alignment padding
};

static_assert(sizeof(UdpMetadata) == 48, "UdpMetadata must be exactly 48 bytes");

} // namespace Core::Net

namespace Core::Serialization {

template <>
struct TypeTraits<Core::Net::UdpMetadata> {
    static constexpr bool isReflected = true;
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("src_ip", &Core::Net::UdpMetadata::srcIp),
            Core::Serialization::field("src_port", &Core::Net::UdpMetadata::srcPort),
            Core::Serialization::field("dst_port", &Core::Net::UdpMetadata::dstPort),
            Core::Serialization::field("dst_ip", &Core::Net::UdpMetadata::dstIp),
            Core::Serialization::field("address_family", &Core::Net::UdpMetadata::addressFamily));
    }
};

} // namespace Core::Serialization

namespace Backend {

template <>
struct MetadataTraits<Core::Net::UdpMetadata> {
    static constexpr bool isRegistered = true;
    static constexpr ProtocolId protocol { static_cast<std::uint8_t>(IPPROTO_UDP) };
};

} // namespace Backend
