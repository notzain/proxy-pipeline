#pragma once

#include <cstdint>

namespace Core::Message {

/**
 * @brief Network transport metadata captured by UDP Ingress and restored at UDP Egress.
 *
 * All IP and port numbers are stored in network byte order (big-endian).
 */
struct alignas(4) UdpMetadata {
    std::uint32_t srcIp { 0 }; ///< Originating client IPv4 address
    std::uint16_t srcPort { 0 }; ///< Originating client UDP port
    std::uint16_t dstPort { 0 }; ///< Target / ingress UDP port
    std::uint32_t dstIp { 0 }; ///< Target / ingress IPv4 address
};

static_assert(sizeof(UdpMetadata) == 12, "UdpMetadata must be exactly 12 bytes");

} // namespace Core::Message
