#pragma once

#include "common/compat/Span.hpp"
#include "core/simulation/PcapFormat.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace Core::Simulation {

/**
 * @brief Represents an extracted network packet from a PCAP capture.
 */
struct CapturedPacket {
    std::uint64_t timestampNs { 0 }; ///< Nanosecond-resolution timestamp
    std::uint32_t srcIp { 0 }; ///< IPv4 source address in network byte order
    std::uint32_t dstIp { 0 }; ///< IPv4 destination address in network byte order
    std::uint16_t srcPort { 0 }; ///< UDP source port in network byte order
    std::uint16_t dstPort { 0 }; ///< UDP destination port in network byte order
    std::vector<std::byte> payload; ///< Application UDP payload
};

/**
 * @brief High-performance native reader for classical PCAP files.
 */
class PcapReader {
public:
    PcapReader();
    ~PcapReader();

    /**
     * @brief Opens a PCAP file for reading.
     * @param filePath Path to the .pcap capture file.
     * @return true if successfully opened and global header validated.
     */
    bool open(const std::string& filePath);

    /**
     * @brief Closes the open PCAP file.
     */
    void close();

    /**
     * @brief Checks if a PCAP file is currently open and valid.
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Reads the next UDP packet from the capture file.
     * Non-UDP packets or non-IPv4 Ethernet packets are skipped automatically.
     * @return CapturedPacket if available, or std::nullopt at EOF or error.
     */
    std::optional<CapturedPacket> readNextUdpPacket();

    /**
     * @brief Reads all remaining UDP packets from the capture file into a vector.
     */
    std::vector<CapturedPacket> readAllUdpPackets();

    /**
     * @brief Rewinds reader back to the first packet in the capture file.
     */
    bool rewind();

    [[nodiscard]] const PcapFileHeader& header() const noexcept;
    [[nodiscard]] bool isNanosecondPrecision() const noexcept;

private:
    std::ifstream m_file;
    PcapFileHeader m_header { };
    bool m_isOpen { false };
    bool m_byteSwapped { false };
    bool m_nanosecondPrecision { false };
};

} // namespace Core::Simulation
