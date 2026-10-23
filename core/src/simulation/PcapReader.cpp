#include "core/simulation/PcapReader.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace Core::Simulation {

namespace {

    inline std::uint16_t swap16(std::uint16_t val)
    {
        return static_cast<std::uint16_t>((val << 8) | (val >> 8));
    }

    inline std::uint32_t swap32(std::uint32_t val)
    {
        return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) | ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
    }

} // namespace

PcapReader::PcapReader() = default;

PcapReader::~PcapReader()
{
    close();
}

bool PcapReader::open(const std::string& filePath)
{
    close();

    m_file.open(filePath, std::ios::binary);
    if (!m_file.is_open()) {
        return false;
    }

    if (!m_file.read(reinterpret_cast<char*>(&m_header), sizeof(PcapFileHeader))) {
        close();
        return false;
    }

    if (m_header.magic == PcapMagicMicroseconds) {
        m_byteSwapped = false;
        m_nanosecondPrecision = false;
    } else if (m_header.magic == PcapMagicNanoseconds) {
        m_byteSwapped = false;
        m_nanosecondPrecision = true;
    } else if (m_header.magic == PcapMagicSwappedMicro) {
        m_byteSwapped = true;
        m_nanosecondPrecision = false;
    } else if (m_header.magic == PcapMagicSwappedNano) {
        m_byteSwapped = true;
        m_nanosecondPrecision = true;
    } else {
        // Unrecognized PCAP magic number
        close();
        return false;
    }

    if (m_byteSwapped) {
        m_header.versionMajor = swap16(m_header.versionMajor);
        m_header.versionMinor = swap16(m_header.versionMinor);
        m_header.snapLen = swap32(m_header.snapLen);
        m_header.network = swap32(m_header.network);
    }

    m_isOpen = true;
    return true;
}

void PcapReader::close()
{
    if (m_file.is_open()) {
        m_file.close();
    }
    m_isOpen = false;
    m_byteSwapped = false;
    m_nanosecondPrecision = false;
}

bool PcapReader::isOpen() const noexcept
{
    return m_isOpen && m_file.is_open();
}

bool PcapReader::rewind()
{
    if (!isOpen()) {
        return false;
    }
    m_file.clear();
    m_file.seekg(sizeof(PcapFileHeader), std::ios::beg);
    return m_file.good();
}

const PcapFileHeader& PcapReader::header() const noexcept
{
    return m_header;
}

bool PcapReader::isNanosecondPrecision() const noexcept
{
    return m_nanosecondPrecision;
}

std::optional<CapturedPacket> PcapReader::readNextUdpPacket()
{
    while (isOpen() && m_file.good()) {
        PcapPacketHeader pktHdr { };
        if (!m_file.read(reinterpret_cast<char*>(&pktHdr), sizeof(PcapPacketHeader))) {
            return std::nullopt; // EOF or read error
        }

        if (m_byteSwapped) {
            pktHdr.tsSec = swap32(pktHdr.tsSec);
            pktHdr.tsUsec = swap32(pktHdr.tsUsec);
            pktHdr.inclLen = swap32(pktHdr.inclLen);
            pktHdr.origLen = swap32(pktHdr.origLen);
        }

        if (pktHdr.inclLen > 65536) {
            return std::nullopt; // Corrupted packet length
        }

        std::vector<std::byte> rawBuffer(pktHdr.inclLen);
        if (!m_file.read(reinterpret_cast<char*>(rawBuffer.data()), pktHdr.inclLen)) {
            return std::nullopt;
        }

        // Must at least cover Ethernet header (14 bytes)
        if (rawBuffer.size() < sizeof(EthernetHeader)) {
            continue;
        }

        const auto* eth = reinterpret_cast<const EthernetHeader*>(rawBuffer.data());
        std::uint16_t etherType = ntohs(eth->etherType);

        // VLAN tag handling (0x8100)
        std::size_t l3Offset = sizeof(EthernetHeader);
        if (etherType == 0x8100 && rawBuffer.size() >= l3Offset + 4) {
            etherType = ntohs(*reinterpret_cast<const std::uint16_t*>(rawBuffer.data() + l3Offset + 2));
            l3Offset += 4;
        }

        // Only parse IPv4 (0x0800)
        if (etherType != 0x0800 || rawBuffer.size() < l3Offset + sizeof(Ipv4Header)) {
            continue;
        }

        const auto* ip = reinterpret_cast<const Ipv4Header*>(rawBuffer.data() + l3Offset);
        std::uint8_t ipVersion = (ip->versionAndIhl >> 4) & 0x0F;
        std::uint8_t ihlBytes = (ip->versionAndIhl & 0x0F) * 4;

        if (ipVersion != 4 || ihlBytes < sizeof(Ipv4Header) || rawBuffer.size() < l3Offset + ihlBytes) {
            continue;
        }

        // Only parse UDP (protocol 17)
        if (ip->protocol != 17) {
            continue;
        }

        std::size_t udpOffset = l3Offset + ihlBytes;
        if (rawBuffer.size() < udpOffset + sizeof(UdpPacketHeader)) {
            continue;
        }

        const auto* udp = reinterpret_cast<const UdpPacketHeader*>(rawBuffer.data() + udpOffset);
        std::uint16_t udpLen = ntohs(udp->length);
        if (udpLen < sizeof(UdpPacketHeader)) {
            continue;
        }

        std::size_t payloadOffset = udpOffset + sizeof(UdpPacketHeader);
        std::size_t payloadLen = udpLen - sizeof(UdpPacketHeader);

        if (rawBuffer.size() < payloadOffset + payloadLen) {
            // Truncated packet
            payloadLen = rawBuffer.size() - payloadOffset;
        }

        CapturedPacket pkt;
        uint64_t secNs = static_cast<uint64_t>(pktHdr.tsSec) * 1'000'000'000ULL;
        uint64_t fracNs = m_nanosecondPrecision ? pktHdr.tsUsec : (static_cast<uint64_t>(pktHdr.tsUsec) * 1'000ULL);
        pkt.timestampNs = secNs + fracNs;
        pkt.srcIp = ip->srcIp;
        pkt.dstIp = ip->dstIp;
        pkt.srcPort = udp->srcPort;
        pkt.dstPort = udp->dstPort;

        pkt.payload.resize(payloadLen);
        if (payloadLen > 0) {
            std::memcpy(pkt.payload.data(), rawBuffer.data() + payloadOffset, payloadLen);
        }

        return pkt;
    }

    return std::nullopt;
}

std::vector<CapturedPacket> PcapReader::readAllUdpPackets()
{
    std::vector<CapturedPacket> packets;
    while (auto pkt = readNextUdpPacket()) {
        packets.push_back(std::move(*pkt));
    }
    return packets;
}

} // namespace Core::Simulation
