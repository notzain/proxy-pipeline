#pragma once

#include <cstdint>

namespace Core::Simulation {

#pragma pack(push, 1)

/**
 * @brief Standard 24-byte PCAP file header.
 */
struct PcapFileHeader {
    std::uint32_t magic { 0xa1b2c3d4 }; ///< Magic number (0xa1b2c3d4 = microsecond, 0xa1b23c4d = nanosecond)
    std::uint16_t versionMajor { 2 }; ///< Major version (typically 2)
    std::uint16_t versionMinor { 4 }; ///< Minor version (typically 4)
    std::int32_t thisZone { 0 }; ///< GMT to local correction (seconds)
    std::uint32_t sigFigs { 0 }; ///< Accuracy of timestamps
    std::uint32_t snapLen { 65535 }; ///< Max length of captured packets
    std::uint32_t network { 1 }; ///< Data link type (1 = Ethernet DLT_EN10MB)
};

/**
 * @brief Standard 16-byte PCAP per-packet record header.
 */
struct PcapPacketHeader {
    std::uint32_t tsSec { 0 }; ///< Timestamp seconds
    std::uint32_t tsUsec { 0 }; ///< Timestamp microseconds (or nanoseconds if magic is 0xa1b23c4d)
    std::uint32_t inclLen { 0 }; ///< Captured octets saved in file
    std::uint32_t origLen { 0 }; ///< Actual length of packet off wire
};

/**
 * @brief 14-byte Ethernet frame header.
 */
struct EthernetHeader {
    std::uint8_t destMac[6] { 0 };
    std::uint8_t srcMac[6] { 0 };
    std::uint16_t etherType { 0 }; ///< 0x0800 = IPv4, 0x86dd = IPv6 (Big-Endian)
};

/**
 * @brief Minimum 20-byte IPv4 packet header.
 */
struct Ipv4Header {
    std::uint8_t versionAndIhl { 0x45 }; ///< Version 4, IHL 5 (20 bytes)
    std::uint8_t typeOfService { 0 };
    std::uint16_t totalLength { 0 };
    std::uint16_t identification { 0 };
    std::uint16_t flagsAndFragment { 0 };
    std::uint8_t ttl { 64 };
    std::uint8_t protocol { 17 }; ///< 17 = UDP
    std::uint16_t checksum { 0 };
    std::uint32_t srcIp { 0 }; ///< Network byte order
    std::uint32_t dstIp { 0 }; ///< Network byte order
};

/**
 * @brief Standard 8-byte UDP packet header.
 */
struct UdpPacketHeader {
    std::uint16_t srcPort { 0 }; ///< Network byte order
    std::uint16_t dstPort { 0 }; ///< Network byte order
    std::uint16_t length { 0 }; ///< Length of UDP header + payload (Big-Endian)
    std::uint16_t checksum { 0 };
};

#pragma pack(pop)

static_assert(sizeof(PcapFileHeader) == 24, "PcapFileHeader must be 24 bytes");
static_assert(sizeof(PcapPacketHeader) == 16, "PcapPacketHeader must be 16 bytes");
static_assert(sizeof(EthernetHeader) == 14, "EthernetHeader must be 14 bytes");
static_assert(sizeof(Ipv4Header) == 20, "Ipv4Header must be 20 bytes");
static_assert(sizeof(UdpPacketHeader) == 8, "UdpPacketHeader must be 8 bytes");

inline constexpr std::uint32_t PcapMagicMicroseconds = 0xa1b2c3d4;
inline constexpr std::uint32_t PcapMagicNanoseconds = 0xa1b23c4d;
inline constexpr std::uint32_t PcapMagicSwappedMicro = 0xd4c3b2a1;
inline constexpr std::uint32_t PcapMagicSwappedNano = 0x4d3cb2a1;

} // namespace Core::Simulation
