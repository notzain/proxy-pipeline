#include "core/net/UdpInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include "backend/SocketAddress.hpp"
#include "core/message/EnvelopeHeader.hpp"
#include "core/message/UdpMetadata.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Core::Net {

namespace {

    constexpr std::size_t EnvelopePreambleSize = sizeof(Message::EnvelopeHeader) + sizeof(Message::UdpMetadata);

    std::uint64_t getCurrentTimeNs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    void writeEnvelopePreamble(
        std::span<std::byte> destination,
        std::size_t payloadSize,
        const Backend::SocketAddress& clientAddr,
        const Backend::SocketAddress& boundAddr)
    {
        Message::EnvelopeHeader header { };
        header.magic = Message::EnvelopeMagic;
        header.version = Message::EnvelopeVersion;
        header.protocol = Message::ProtocolId::Udp;
        header.metadataLength = sizeof(Message::UdpMetadata);
        header.payloadLength = static_cast<std::uint32_t>(payloadSize);
        header.timestampNs = getCurrentTimeNs();

        Message::UdpMetadata meta { };
        meta.srcPort = clientAddr.portNet();
        meta.dstPort = boundAddr.portNet();
        if (clientAddr.family() == Backend::AddressFamily::Ipv6) {
            meta.addressFamily = 6;
            std::memcpy(meta.srcIpv6, clientAddr.ipv6Bytes(), sizeof(meta.srcIpv6));
        } else {
            meta.addressFamily = 4;
            meta.srcIp = clientAddr.ipv4Net();
        }
        if (boundAddr.family() == Backend::AddressFamily::Ipv6) {
            std::memcpy(meta.dstIpv6, boundAddr.ipv6Bytes(), sizeof(meta.dstIpv6));
        } else {
            meta.dstIp = boundAddr.ipv4Net();
        }

        std::memcpy(destination.data(), &header, sizeof(Message::EnvelopeHeader));
        std::memcpy(destination.data() + sizeof(Message::EnvelopeHeader), &meta, sizeof(Message::UdpMetadata));
    }

} // namespace

UdpInputChannel::UdpInputChannel(std::string name, std::string host, std::uint16_t port, std::size_t receiveBufferSize)
    : ::Backend::IInputChannel(std::move(name))
    , m_host(std::move(host))
    , m_port(port)
    , m_receiveBufferSize(receiveBufferSize)
    , m_poller(::Backend::createSocketPoller())
{
    openAndBindSocket();
}

UdpInputChannel::UdpInputChannel(std::string host, std::uint16_t port, std::size_t receiveBufferSize)
    : UdpInputChannel("udp_ingress", std::move(host), port, receiveBufferSize)
{
}

UdpInputChannel::~UdpInputChannel() = default;

UdpInputChannel::UdpInputChannel(UdpInputChannel&& other) noexcept
    : m_host(std::move(other.m_host))
    , m_port(other.m_port)
    , m_receiveBufferSize(other.m_receiveBufferSize)
    , m_socketFd(std::move(other.m_socketFd))
    , m_boundAddr(other.m_boundAddr)
    , m_poller(std::move(other.m_poller))
{
}

UdpInputChannel& UdpInputChannel::operator=(UdpInputChannel&& other) noexcept
{
    if (this != &other) {
        m_host = std::move(other.m_host);
        m_port = other.m_port;
        m_receiveBufferSize = other.m_receiveBufferSize;
        m_socketFd = std::move(other.m_socketFd);
        m_boundAddr = other.m_boundAddr;
        m_poller = std::move(other.m_poller);
    }
    return *this;
}

void UdpInputChannel::closeSocket()
{
    m_socketFd.reset();
}

void UdpInputChannel::openAndBindSocket()
{
    closeSocket();

    if (m_host == "0.0.0.0" || m_host.empty()) {
        m_boundAddr = Backend::SocketAddress::fromIpv4Net(INADDR_ANY, htons(m_port));
    } else if (m_host == "::") {
        in6_addr any6 = IN6ADDR_ANY_INIT;
        m_boundAddr = Backend::SocketAddress::fromIpv6Net(reinterpret_cast<const std::uint8_t*>(&any6), htons(m_port));
    } else {
        m_boundAddr = Backend::SocketAddress::fromString(m_host, m_port);
    }

    const int domain = (m_boundAddr.family() == Backend::AddressFamily::Ipv6) ? AF_INET6 : AF_INET;
    m_socketFd.reset(::socket(domain, SOCK_DGRAM, 0));
    if (!m_socketFd) {
        Proxy::Log::error("[UdpInputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int reuse = 1;
    ::setsockopt(m_socketFd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(m_socketFd.get(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    int rcvbuf = static_cast<int>(m_receiveBufferSize);
    ::setsockopt(m_socketFd.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_storage ss { };
    std::size_t nativeLen = m_boundAddr.toNative(&ss, sizeof(ss));
    if (nativeLen == 0 || ::bind(m_socketFd.get(), reinterpret_cast<sockaddr*>(&ss), static_cast<socklen_t>(nativeLen)) < 0) {
        Proxy::Log::error("[UdpInputChannel] Failed to bind UDP socket to {}:{} ({})", m_host, m_port, std::strerror(errno));
        closeSocket();
        return;
    }

    Proxy::Log::info("[UdpInputChannel] Bound and listening on UDP {}:{}", m_host, m_port);
}

std::size_t UdpInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (!m_socketFd) {
        openAndBindSocket();
        if (!m_socketFd) {
            return 0;
        }
    }

    if (destination.size() <= EnvelopePreambleSize || !m_poller) {
        return 0;
    }

    Backend::SocketAddress clientAddr;
    std::size_t payloadBytes = m_poller->receive(
        m_socketFd.get(),
        destination.subspan(EnvelopePreambleSize),
        timeout,
        &clientAddr);

    if (payloadBytes == 0) {
        return 0;
    }

    writeEnvelopePreamble(destination, payloadBytes, clientAddr, m_boundAddr);
    return EnvelopePreambleSize + payloadBytes;
}

std::size_t UdpInputChannel::receiveBatch(
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (!m_socketFd) {
        openAndBindSocket();
        if (!m_socketFd) {
            return 0;
        }
    }

    if (destinations.empty() || outSizes.size() < destinations.size() || !m_poller) {
        return 0;
    }

    constexpr std::size_t MaxBatch = 32;
    const std::size_t batchSize = std::min(destinations.size(), MaxBatch);

    std::array<std::span<std::byte>, MaxBatch> payloadDests;
    std::array<std::size_t, MaxBatch> payloadSizes { };
    std::array<Backend::SocketAddress, MaxBatch> clientAddrs;

    for (std::size_t i = 0; i < batchSize; ++i) {
        if (destinations[i].size() <= EnvelopePreambleSize) {
            return 0;
        }
        payloadDests[i] = destinations[i].subspan(EnvelopePreambleSize);
    }

    std::size_t count = m_poller->receiveBatch(
        m_socketFd.get(),
        std::span<const std::span<std::byte>>(payloadDests.data(), batchSize),
        std::span<std::size_t>(payloadSizes.data(), batchSize),
        timeout,
        std::span<Backend::SocketAddress>(clientAddrs.data(), batchSize));

    for (std::size_t i = 0; i < count; ++i) {
        writeEnvelopePreamble(destinations[i], payloadSizes[i], clientAddrs[i], m_boundAddr);
        outSizes[i] = EnvelopePreambleSize + payloadSizes[i];
    }

    return count;
}

int UdpInputChannel::getNativeSocket() const noexcept
{
    return m_socketFd.get();
}

const std::string& UdpInputChannel::getHost() const noexcept
{
    return m_host;
}

std::uint16_t UdpInputChannel::getPort() const noexcept
{
    return m_port;
}

} // namespace Core::Net
