#include "core/net/UdpInputChannel.hpp"
#include "backend/channel/PollerResultBridge.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "common/io/WireFormat.hpp"
#include "common/net/ISocketPoller.hpp"
#include "common/net/SocketAddress.hpp"
#include "core/net/UdpMetadata.hpp"
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

    constexpr std::size_t EnvelopePreambleSize = sizeof(Backend::EnvelopeHeader) + sizeof(Core::Net::UdpMetadata);

    /**
     * @brief Writes the 18-byte envelope header and UdpMetadata preamble into destination.
     *
     * Automatically stamps current UTC time (system_clock since UNIX epoch) via writeEnvelopeHeader.
     *
     * @param destination Target buffer span to receive the preamble.
     * @param payloadSize Length of the incoming datagram payload.
     * @param clientAddr Source socket address of the remote sender.
     * @param boundAddr Local bound socket address receiving the packet.
     */
    void writeEnvelopePreamble(
        Common::Compat::span<std::byte> destination,
        std::size_t payloadSize,
        const Common::Net::SocketAddress& clientAddr,
        const Common::Net::SocketAddress& boundAddr)
    {
        Backend::writeEnvelopeHeader(
            destination,
            Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol,
            static_cast<std::uint16_t>(sizeof(Core::Net::UdpMetadata)),
            static_cast<std::uint32_t>(payloadSize));

        Core::Net::UdpMetadata meta { };
        meta.srcPort = clientAddr.portNet();
        meta.dstPort = boundAddr.portNet();
        if (clientAddr.family() == Common::Net::AddressFamily::Ipv6) {
            meta.addressFamily = 6;
            std::memcpy(meta.srcIpv6, clientAddr.ipv6Bytes(), sizeof(meta.srcIpv6));
        } else {
            meta.addressFamily = 4;
            meta.srcIp = clientAddr.ipv4Net();
        }
        if (boundAddr.family() == Common::Net::AddressFamily::Ipv6) {
            std::memcpy(meta.dstIpv6, boundAddr.ipv6Bytes(), sizeof(meta.dstIpv6));
        } else {
            meta.dstIp = boundAddr.ipv4Net();
        }

        std::memcpy(destination.data() + sizeof(Backend::EnvelopeHeader), &meta, sizeof(Core::Net::UdpMetadata));
    }

} // namespace

UdpInputChannel::UdpInputChannel(std::string name, std::string host, std::uint16_t port, std::size_t receiveBufferSize)
    : ::Backend::IInputChannel(std::move(name))
    , m_host(std::move(host))
    , m_port(port)
    , m_receiveBufferSize(receiveBufferSize)
    , m_poller(::Common::Net::createSocketPoller())
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
        m_boundAddr = Common::Net::SocketAddress::fromIpv4Net(INADDR_ANY, htons(m_port));
    } else if (m_host == "::") {
        in6_addr any6 = IN6ADDR_ANY_INIT;
        m_boundAddr = Common::Net::SocketAddress::fromIpv6Net(reinterpret_cast<const std::uint8_t*>(&any6), htons(m_port));
    } else {
        m_boundAddr = Common::Net::SocketAddress::fromString(m_host, m_port);
    }

    const int domain = (m_boundAddr.family() == Common::Net::AddressFamily::Ipv6) ? AF_INET6 : AF_INET;
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

    Common::Net::SocketAddress clientAddr;
    Common::Net::PollerBytesResult receiveResult = m_poller->receive(
        m_socketFd.get(),
        destination.subspan(EnvelopePreambleSize),
        timeout,
        &clientAddr);

    if (!receiveResult.hasValue()) {
        return ::Backend::fromPollerBytesResult(receiveResult);
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

    std::array<Common::Net::PollerBufferSlot, MaxBatch> payloadSlots;
    std::array<Common::Net::SocketAddress, MaxBatch> clientAddrs;

    for (std::size_t i = 0; i < batchSize; ++i) {
        if (slots[i].buffer.size() <= EnvelopePreambleSize) {
            return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
        }
        payloadSlots[i] = Common::Net::PollerBufferSlot { slots[i].buffer.subspan(EnvelopePreambleSize), 0 };
    }

    Common::Net::PollerBatchResult result = m_poller->receiveBatch(
        m_socketFd.get(),
        Common::Compat::span<Common::Net::PollerBufferSlot>(payloadSlots.data(), batchSize),
        timeout,
        Common::Compat::span<Common::Net::SocketAddress>(clientAddrs.data(), batchSize));

    const std::size_t receivedCount = result.hasValue() ? result.value() : result.error().receivedCount;
    for (std::size_t i = 0; i < receivedCount; ++i) {
        writeEnvelopePreamble(slots[i].buffer, payloadSlots[i].size, clientAddrs[i], m_boundAddr);
        slots[i].size = EnvelopePreambleSize + payloadSlots[i].size;
    }

    return ::Backend::fromPollerBatchResult(result);
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
