#include "core/net/UdpInputChannel.hpp"
#include "backend/EnvelopeHeader.hpp"
#include "backend/ISocketPoller.hpp"
#include "backend/SocketAddress.hpp"
#include "backend/UdpMetadata.hpp"
#include "common/io/WireFormat.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace Core::Net {

namespace {

    constexpr std::size_t EnvelopePreambleSize = sizeof(Backend::EnvelopeHeader) + sizeof(Backend::UdpMetadata);

    std::uint64_t getCurrentTimeNs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    void writeEnvelopePreamble(
        Common::Compat::span<std::byte> destination,
        std::size_t payloadSize,
        const Backend::SocketAddress& clientAddr,
        const Backend::SocketAddress& boundAddr)
    {
        std::byte* dst = destination.data();
        Common::IO::Wire::writeUint16Be(dst, Backend::EnvelopeMagic);
        dst[2] = static_cast<std::byte>(Backend::EnvelopeVersion);
        dst[3] = static_cast<std::byte>(Backend::ProtocolId::Udp);
        Common::IO::Wire::writeUint16Be(dst + 4, static_cast<std::uint16_t>(sizeof(Backend::UdpMetadata)));
        Common::IO::Wire::writeUint32Be(dst + 6, static_cast<std::uint32_t>(payloadSize));
        Common::IO::Wire::writeUint64Be(dst + 10, getCurrentTimeNs());

        Backend::UdpMetadata meta { };
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

        std::memcpy(dst + sizeof(Backend::EnvelopeHeader), &meta, sizeof(Backend::UdpMetadata));
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
    : ::Backend::IInputChannel(std::move(other))
    , m_host(std::move(other.m_host))
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
        ::Backend::IInputChannel::operator=(std::move(other));
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

Backend::BytesResult UdpInputChannel::receiveSlot(::Backend::BufferSlot& slot, Timeout timeout)
{
    slot.size = 0;
    const auto destination = slot.buffer;
    if (!m_socketFd) {
        openAndBindSocket();
        if (!m_socketFd) {
            return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::bad_file_descriptor)));
        }
    }

    if (destination.size() <= EnvelopePreambleSize) {
        return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)));
    }
    if (!m_poller) {
        return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::not_connected)));
    }

    Backend::SocketAddress clientAddr;
    BytesResult receiveResult = m_poller->receive(
        m_socketFd.get(),
        destination.subspan(EnvelopePreambleSize),
        timeout,
        &clientAddr);

    if (!receiveResult.hasValue()) {
        return BytesResult::failure(receiveResult.error());
    }

    writeEnvelopePreamble(destination, receiveResult.value(), clientAddr, m_boundAddr);
    slot.size = EnvelopePreambleSize + receiveResult.value();
    return BytesResult::success(slot.size);
}

Backend::BatchResult UdpInputChannel::receiveBatch(
    Common::Compat::span<::Backend::BufferSlot> slots,
    Timeout timeout)
{
    if (!m_socketFd) {
        openAndBindSocket();
        if (!m_socketFd) {
            return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::bad_file_descriptor)) });
        }
    }

    if (slots.empty()) {
        return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
    }
    if (!m_poller) {
        return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::not_connected)) });
    }

    constexpr std::size_t MaxBatch = 32;
    const std::size_t batchSize = std::min(slots.size(), MaxBatch);

    std::array<::Backend::BufferSlot, MaxBatch> payloadSlots;
    std::array<Backend::SocketAddress, MaxBatch> clientAddrs;

    for (std::size_t i = 0; i < batchSize; ++i) {
        if (slots[i].buffer.size() <= EnvelopePreambleSize) {
            return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
        }
        payloadSlots[i] = ::Backend::BufferSlot { slots[i].buffer.subspan(EnvelopePreambleSize), 0 };
    }

    BatchResult result = m_poller->receiveBatch(
        m_socketFd.get(),
        Common::Compat::span<::Backend::BufferSlot>(payloadSlots.data(), batchSize),
        timeout,
        Common::Compat::span<Backend::SocketAddress>(clientAddrs.data(), batchSize));

    const std::size_t receivedCount = result.hasValue() ? result.value() : result.error().receivedCount;
    for (std::size_t i = 0; i < receivedCount; ++i) {
        writeEnvelopePreamble(slots[i].buffer, payloadSlots[i].size, clientAddrs[i], m_boundAddr);
        slots[i].size = EnvelopePreambleSize + payloadSlots[i].size;
    }

    return result;
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
