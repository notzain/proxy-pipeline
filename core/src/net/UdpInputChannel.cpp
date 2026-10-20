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
        meta.srcIp = clientAddr.ipv4Net();
        meta.srcPort = clientAddr.portNet();
        meta.dstIp = boundAddr.ipv4Net();
        meta.dstPort = boundAddr.portNet();

        std::memcpy(destination.data(), &header, sizeof(Message::EnvelopeHeader));
        std::memcpy(destination.data() + sizeof(Message::EnvelopeHeader), &meta, sizeof(Message::UdpMetadata));
    }

} // namespace

UdpInputChannel::UdpInputChannel(std::string host, std::uint16_t port, std::size_t receiveBufferSize)
    : m_host(std::move(host))
    , m_port(port)
    , m_receiveBufferSize(receiveBufferSize)
    , m_poller(::Backend::createSocketPoller())
{
    openAndBindSocket();
}

UdpInputChannel::~UdpInputChannel()
{
    closeSocket();
}

UdpInputChannel::UdpInputChannel(UdpInputChannel&& other) noexcept
    : m_host(std::move(other.m_host))
    , m_port(other.m_port)
    , m_receiveBufferSize(other.m_receiveBufferSize)
    , m_socketFd(other.m_socketFd)
    , m_boundAddr(other.m_boundAddr)
    , m_poller(std::move(other.m_poller))
{
    other.m_socketFd = -1;
}

UdpInputChannel& UdpInputChannel::operator=(UdpInputChannel&& other) noexcept
{
    if (this != &other) {
        closeSocket();
        m_host = std::move(other.m_host);
        m_port = other.m_port;
        m_receiveBufferSize = other.m_receiveBufferSize;
        m_socketFd = other.m_socketFd;
        m_boundAddr = other.m_boundAddr;
        m_poller = std::move(other.m_poller);
        other.m_socketFd = -1;
    }
    return *this;
}

void UdpInputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void UdpInputChannel::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[UdpInputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int reuse = 1;
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    int rcvbuf = static_cast<int>(m_receiveBufferSize);
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    m_boundAddr = (m_host == "0.0.0.0" || m_host.empty())
        ? Backend::SocketAddress::fromIpv4Net(INADDR_ANY, htons(m_port))
        : Backend::SocketAddress::fromString(m_host, m_port);

    sockaddr_storage ss { };
    std::size_t nativeLen = m_boundAddr.toNative(&ss, sizeof(ss));
    if (nativeLen == 0 || ::bind(m_socketFd, reinterpret_cast<sockaddr*>(&ss), static_cast<socklen_t>(nativeLen)) < 0) {
        Proxy::Log::error("[UdpInputChannel] Failed to bind UDP socket to {}:{} ({})", m_host, m_port, std::strerror(errno));
        closeSocket();
        return;
    }

    Proxy::Log::info("[UdpInputChannel] Bound and listening on UDP {}:{}", m_host, m_port);
}

std::optional<Backend::IInputChannel::Buffer> UdpInputChannel::receive(Timeout timeout)
{
    Buffer buffer(65535);
    std::size_t bytes = receiveInto(std::span<std::byte>(buffer.data(), buffer.size()), timeout);
    if (bytes == 0) {
        return std::nullopt;
    }
    buffer.resize(bytes);
    return buffer;
}

std::size_t UdpInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return 0;
        }
    }

    if (destination.size() <= EnvelopePreambleSize || !m_poller) {
        return 0;
    }

    Backend::SocketAddress clientAddr;
    std::size_t payloadBytes = m_poller->receive(
        m_socketFd,
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
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
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
        m_socketFd,
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
    return m_socketFd;
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
