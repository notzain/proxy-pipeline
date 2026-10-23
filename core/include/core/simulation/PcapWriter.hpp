#pragma once

#include "core/simulation/PcapFormat.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>

namespace Core::Simulation {

/**
 * @brief High-performance native writer for creating PCAP captures.
 *
 * Useful for recording live or simulated test traffic into standard .pcap files
 * inspectable via Wireshark or replayable via PcapReader / tcpreplay.
 */
class PcapWriter {
public:
    PcapWriter();
    ~PcapWriter();

    /**
     * @brief Opens or creates a PCAP file for writing.
     * Writes standard 24-byte PCAP header with microsecond or nanosecond resolution.
     */
    bool open(const std::string& filePath, bool nanosecondPrecision = true);

    /**
     * @brief Closes the PCAP file.
     */
    void close();

    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Writes a raw Ethernet packet to the PCAP file.
     */
    bool writeRawPacket(std::span<const std::byte> packetBytes, std::uint64_t timestampNs);

    /**
     * @brief Constructs and writes a complete Ethernet + IPv4 + UDP packet with the given payload.
     *
     * @param srcIp IPv4 source address in network byte order.
     * @param dstIp IPv4 destination address in network byte order.
     * @param srcPort UDP source port in network byte order.
     * @param dstPort UDP destination port in network byte order.
     * @param payload Application payload bytes.
     * @param timestampNs Packet timestamp in nanoseconds.
     */
    bool writeUdpPacket(
        std::uint32_t srcIp,
        std::uint32_t dstIp,
        std::uint16_t srcPort,
        std::uint16_t dstPort,
        std::span<const std::byte> payload,
        std::uint64_t timestampNs);

    /**
     * @brief Flushes underlying file buffer.
     */
    void flush();

private:
    std::ofstream m_file;
    bool m_isOpen { false };
    bool m_nanosecondPrecision { true };
};

} // namespace Core::Simulation

