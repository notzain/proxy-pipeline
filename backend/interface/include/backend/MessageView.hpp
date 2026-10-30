#pragma once

#include "backend/EnvelopeHeader.hpp"
#include "backend/Lease.hpp"
#include "backend/ProtocolId.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/Borrow.hpp"
#include "common/memory/ObserverPtr.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace Backend {

/**
 * @brief Zero-copy, non-owning view representing an in-flight message.
 *
 * Exposes the application payload as its primary field to users, backends, and handlers.
 * Can be constructed from native Proxy envelope wire buffers or directly by
 * custom backend transports and codecs. Encapsulates an optional transport Lease so
 * output channels can consume and forward the underlying message object transparently.
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
        Common::Compat::span<const std::byte> metadata = { }) noexcept;

    MessageView(
        Common::Compat::span<const std::byte> rawBuffer,
        Common::Compat::span<const std::byte> metadata,
        Common::Compat::span<const std::byte> payload,
        ProtocolId protocol,
        std::uint64_t timestampNs) noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> payload() const noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> rawBuffer() const noexcept;

    [[nodiscard]] Common::Compat::span<const std::byte> metadata() const noexcept;

    [[nodiscard]] ProtocolId protocol() const noexcept;

    [[nodiscard]] std::uint64_t timestampNs() const noexcept;

    /**
     * @brief Binds the in-flight transport lease associated with this message view.
     */
    void attachLease(Common::Memory::Borrow<Lease> lease) noexcept;

    /**
     * @brief Consumes and extracts the underlying transport lease state, if still active.
     *
     * Invoked by output channels that forward the actual transport message object (consuming it).
     * Subsequent calls on the same MessageView return nullptr.
     */
    [[nodiscard]] std::unique_ptr<ILeaseState> consumeLeaseState() const noexcept;

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
    Common::Memory::ObserverPtr<Lease> m_lease { };
};

} // namespace Backend
