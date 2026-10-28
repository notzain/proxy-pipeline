#include "core/simulation/PcapWriter.hpp"
#include "core/simulation/PcapFormat.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <vector>

namespace Core::Simulation {

namespace {

    std::uint16_t computeChecksum(const void* data, std::size_t bytes)
    {
        const auto* ptr = static_cast<const std::uint16_t*>(data);
        std::uint32_t sum = 0;
        while (bytes > 1) {
            sum += *ptr++;
            bytes -= 2;
        }
        if (bytes > 0) {
            sum += *reinterpret_cast<const std::uint8_t*>(ptr);
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return static_cast<std::uint16_t>(~sum);
    }

} // namespace

PcapWriter::PcapWriter() = default;

PcapWriter::~PcapWriter()
{
    close();
}

bool PcapWriter::open(const std::string& filePath, bool nanosecondPrecision)
{
    close();

    m_file.open(filePath, std::ios::binary | std::ios::trunc);
    if (!m_file.is_open()) {
        return false;
    }

    m_nanosecondPrecision = nanosecondPrecision;

    PcapFileHeader hdr { };
    hdr.magic = nanosecondPrecision ? PcapMagicNanoseconds : PcapMagicMicroseconds;
    hdr.versionMajor = 2;
    hdr.versionMinor = 4;
    hdr.thisZone = 0;
    hdr.sigFigs = 0;
    hdr.snapLen = 65535;
    hdr.network = 1; // Ethernet

    if (!m_file.write(reinterpret_cast<const char*>(&hdr), sizeof(PcapFileHeader))) {
        close();
        return false;
    }

    m_isOpen = true;
    return true;
}

void PcapWriter::close()
{
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
    m_isOpen = false;
}

bool PcapWriter::isOpen() const noexcept
{
    return m_isOpen && m_file.is_open();
}

void PcapWriter::flush()
{
    if (isOpen()) {
        m_file.flush();
    }
}

bool PcapWriter::writeRawPacket(Common::Compat::span<const std::byte> packetBytes, std::uint64_t timestampNs)
{
    if (!isOpen() || packetBytes.empty()) {
        return false;
    }

    PcapPacketHeader pktHdr { };
    pktHdr.tsSec = static_cast<std::uint32_t>(timestampNs / 1'000'000'000ULL);
    uint64_t remNs = timestampNs % 1'000'000'000ULL;
    pktHdr.tsUsec = m_nanosecondPrecision ? static_cast<std::uint32_t>(remNs) : static_cast<std::uint32_t>(remNs / 1000ULL);
    pktHdr.inclLen = static_cast<std::uint32_t>(packetBytes.size());
    pktHdr.origLen = static_cast<std::uint32_t>(packetBytes.size());

    if (!m_file.write(reinterpret_cast<const char*>(&pktHdr), sizeof(PcapPacketHeader))) {
        return false;
    }
    if (!m_file.write(reinterpret_cast<const char*>(packetBytes.data()), packetBytes.size())) {
        return false;
    }

    return true;
}

bool PcapWriter::writeUdpPacket(
    std::uint32_t srcIp,
    std::uint32_t dstIp,
    std::uint16_t srcPort,
    std::uint16_t dstPort,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    if (!isOpen()) {
        return false;
    }

    std::size_t totalPacketLen = sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(UdpPacketHeader) + payload.size();
    std::vector<std::byte> frame(totalPacketLen, std::byte { 0 });

    // 1. Ethernet Header
    auto* eth = reinterpret_cast<EthernetHeader*>(frame.data());
    eth->destMac[0] = 0x02; // Locally administered MAC
    eth->srcMac[0] = 0x02;
    eth->etherType = htons(0x0800); // IPv4

    // 2. IPv4 Header
    auto* ip = reinterpret_cast<Ipv4Header*>(frame.data() + sizeof(EthernetHeader));
    ip->versionAndIhl = 0x45; // Version 4, IHL 5
    ip->typeOfService = 0;
    ip->totalLength = htons(static_cast<std::uint16_t>(sizeof(Ipv4Header) + sizeof(UdpPacketHeader) + payload.size()));
    ip->identification = htons(1);
    ip->flagsAndFragment = htons(0x4000); // Don't fragment
    ip->ttl = 64;
    ip->protocol = 17; // UDP
    ip->srcIp = srcIp;
    ip->dstIp = dstIp;
    ip->checksum = 0;
    ip->checksum = computeChecksum(ip, sizeof(Ipv4Header));

    // 3. UDP Header
    auto* udp = reinterpret_cast<UdpPacketHeader*>(frame.data() + sizeof(EthernetHeader) + sizeof(Ipv4Header));
    udp->srcPort = srcPort;
    udp->dstPort = dstPort;
    udp->length = htons(static_cast<std::uint16_t>(sizeof(UdpPacketHeader) + payload.size()));
    udp->checksum = 0; // Checksum optional for IPv4 UDP

    // 4. Payload
    if (!payload.empty()) {
        std::memcpy(frame.data() + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(UdpPacketHeader), payload.data(), payload.size());
    }

    return writeRawPacket(frame, timestampNs);
}

} // namespace Core::Simulation
