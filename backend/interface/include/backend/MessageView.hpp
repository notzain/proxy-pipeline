#pragma once

#include "backend/EnvelopeHeader.hpp"
#include "backend/ProtocolId.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Backend {

/**
 * @brief Zero-copy, non-owning view representing an in-flight message.
 *
 * Exposes the application payload as its primary field to users, backends, and handlers.
 * Can be constructed from native Proxy envelope wire buffers or directly by
 * custom backend transports and codecs.
 */
class MessageView {
public:
    /**
     * @brief Parses and validates an envelope from a raw Proxy binary envelope buffer.
     *
     * @param buffer Non-owning view of raw bytes received from an IPC channel.
     * @return std::optional<MessageView> Valid view if the header and lengths match, or std::nullopt.
     */
    static std::optional<MessageView> from(Common::Compat::span<const std::byte> buffer) noexcept;

    constexpr MessageView() noexcept = default;

    /**
     * @brief Constructs a view from explicit payload and optional raw buffer, timestamp, and metadata.
     */
    explicit MessageView(
        Common::Compat::span<const std::byte> payload,
        Common::Compat::span<const std::byte> rawBuffer = { },
        std::uint64_t timestampNs = 0,
        ProtocolId protocol = ProtocolId::Raw,
        Common::Compat::span<const std::byte> metadata = { }) noexcept
        : m_rawBuffer(rawBuffer.empty() ? payload : rawBuffer)
        , m_metadata(metadata)
        , m_payload(payload)
        , m_timestampNs(timestampNs)
        , m_protocol(protocol)
    {
    }

    MessageView(
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

    [[nodiscard]] Common::Compat::span<const std::byte> payload() const noexcept
    {
        return m_payload;
    }

    [[nodiscard]] Common::Compat::span<const std::byte> rawBuffer() const noexcept
    {
        return m_rawBuffer;
    }

    [[nodiscard]] Common::Compat::span<const std::byte> metadata() const noexcept
    {
        return m_metadata;
    }

    [[nodiscard]] ProtocolId protocol() const noexcept
    {
        return m_protocol;
    }

    [[nodiscard]] std::uint64_t timestampNs() const noexcept
    {
        return m_timestampNs;
    }

    template <typename T>
    [[nodiscard]] const T* getMetadata() const noexcept
    {
        if (m_metadata.size() < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(m_metadata.data());
    }

private:
    Common::Compat::span<const std::byte> m_rawBuffer { };
    Common::Compat::span<const std::byte> m_metadata { };
    Common::Compat::span<const std::byte> m_payload { };
    std::uint64_t m_timestampNs { 0 };
    ProtocolId m_protocol { ProtocolId::Raw };
};

} // namespace Backend

