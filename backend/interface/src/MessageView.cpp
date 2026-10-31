#include "backend/MessageView.hpp"
#include "backend/EnvelopeHeader.hpp"
#include "common/io/WireFormat.hpp"
#include <chrono>
#include <cstring>

namespace Backend {

namespace {

    std::uint64_t getCurrentTimeNs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

} // namespace

std::size_t MessageView::packInto(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    if (destination.data() == nullptr || destination.size() < totalSize) {
        return 0;
    }

    std::byte* dst = destination.data();
    Common::IO::Wire::writeUint16Be(dst, EnvelopeMagic);
    dst[2] = static_cast<std::byte>(EnvelopeVersion);
    dst[3] = static_cast<std::byte>(protocol.value());
    Common::IO::Wire::writeUint16Be(dst + 4, static_cast<std::uint16_t>(metadata.size()));
    Common::IO::Wire::writeUint32Be(dst + 6, static_cast<std::uint32_t>(payload.size()));

    std::uint64_t ts = (timestampNs != 0) ? timestampNs : getCurrentTimeNs();
    Common::IO::Wire::writeUint64Be(dst + 10, ts);

    if (!metadata.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader), metadata.data(), metadata.size());
    }
    if (!payload.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader) + metadata.size(), payload.data(), payload.size());
    }

    return totalSize;
}

std::vector<std::byte> MessageView::pack(
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    std::vector<std::byte> buffer(totalSize);
    packInto(buffer, protocol, metadata, payload, timestampNs);
    return buffer;
}

std::vector<std::byte> MessageView::packRaw(
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    return pack(RawProtocol, { }, payload, timestampNs);
}

std::vector<std::byte> MessageView::withPayload(
    const MessageView& view,
    Common::Compat::span<const std::byte> newPayload)
{
    return pack(view.protocol(), view.metadata(), newPayload, view.timestampNs());
}

std::optional<MessageView> MessageView::from(Common::Compat::span<const std::byte> buffer) noexcept
{
    if (buffer.size() < sizeof(EnvelopeHeader)) {
        return std::nullopt;
    }

    const std::byte* data = buffer.data();
    std::uint16_t magic = Common::IO::Wire::readUint16Be(data);
    std::uint8_t version = static_cast<std::uint8_t>(data[2]);
    if (magic != EnvelopeMagic || version != EnvelopeVersion) {
        return std::nullopt;
    }

    ProtocolId protocol { static_cast<std::uint8_t>(data[3]) };
    std::uint16_t metadataLength = Common::IO::Wire::readUint16Be(data + 4);
    std::uint32_t payloadLength = Common::IO::Wire::readUint32Be(data + 6);
    std::uint64_t timestampNs = Common::IO::Wire::readUint64Be(data + 10);

    std::size_t requiredSize = sizeof(EnvelopeHeader) + metadataLength + payloadLength;
    if (buffer.size() < requiredSize) {
        return std::nullopt;
    }

    const std::byte* base = data + sizeof(EnvelopeHeader);
    auto frameSpan = buffer.subspan(0, requiredSize);
    auto metadataSpan = Common::Compat::span<const std::byte>(base, metadataLength);
    auto payloadSpan = Common::Compat::span<const std::byte>(base + metadataLength, payloadLength);

    return MessageView(frameSpan, metadataSpan, payloadSpan, protocol, timestampNs);
}

MessageView::MessageView(
    Common::Compat::span<const std::byte> payload,
    Common::Compat::span<const std::byte> rawBuffer,
    std::uint64_t timestampNs,
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata) noexcept
    : m_rawBuffer(rawBuffer.empty() ? payload : rawBuffer)
    , m_metadata(metadata)
    , m_payload(payload)
    , m_timestampNs(timestampNs)
    , m_protocol(protocol)
{
}

MessageView::MessageView(
    Common::Compat::span<const std::byte> rawBuffer,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    ProtocolId protocol,
    std::uint64_t timestampNs) noexcept
    : m_rawBuffer(rawBuffer)
    , m_metadata(metadata)
    , m_payload(payload)
    , m_timestampNs(timestampNs)
    , m_protocol(protocol)
{
}

Common::Compat::span<const std::byte> MessageView::payload() const noexcept
{
    return m_payload;
}

Common::Compat::span<const std::byte> MessageView::rawBuffer() const noexcept
{
    return m_rawBuffer;
}

Common::Compat::span<const std::byte> MessageView::metadata() const noexcept
{
    return m_metadata;
}

ProtocolId MessageView::protocol() const noexcept
{
    return m_protocol;
}

std::uint64_t MessageView::timestampNs() const noexcept
{
    return m_timestampNs;
}

void MessageView::attachLease(Common::Memory::Borrow<Lease> lease) noexcept
{
    m_lease = lease;
}

std::unique_ptr<ILeaseState> MessageView::consumeLeaseState() const noexcept
{
    if (!m_lease) {
        return nullptr;
    }
    return m_lease->releaseState();
}

SettlementToken* MessageView::token() const noexcept
{
    if (!m_lease) {
        return nullptr;
    }
    return m_lease->token();
}

Common::Memory::ObserverPtr<ILeaseState> MessageView::leaseState() const noexcept
{
    if (!m_lease) {
        return nullptr;
    }
    return m_lease->state();
}

} // namespace Backend
