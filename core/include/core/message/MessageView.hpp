#pragma once

#include "core/message/EnvelopeHeader.hpp"
#include "core/message/ProtocolId.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace Core::Message {

/**
 * @brief Zero-copy, non-owning view representing an in-flight enveloped message.
 *
 * Provides safe, branchless access to the envelope header, transport metadata,
 * and application payload slices without heap allocation or copying.
 */
class MessageView {
public:
    /**
     * @brief Parses and validates an envelope from a raw memory buffer.
     *
     * @param buffer Non-owning view of raw bytes received from an IPC channel.
     * @return std::optional<MessageView> Valid view if the header and lengths match, or std::nullopt.
     */
    static std::optional<MessageView> from(std::span<const std::byte> buffer) noexcept;

    /**
     * @brief Default constructor creating an empty view.
     */
    constexpr MessageView() noexcept = default;

    /**
     * @brief Constructs a view from explicit pointers and sizes.
     */
    MessageView(std::span<const std::byte> rawBuffer, const EnvelopeHeader* header) noexcept;

    /**
     * @brief Returns the entire raw wire buffer (including EnvelopeHeader, metadata, and payload).
     */
    [[nodiscard]] std::span<const std::byte> rawBuffer() const noexcept;

    /**
     * @brief Returns a view of the application payload body.
     */
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;

    /**
     * @brief Returns a view of the transport metadata section.
     */
    [[nodiscard]] std::span<const std::byte> metadata() const noexcept;

    /**
     * @brief Returns the protocol identifier tag.
     */
    [[nodiscard]] ProtocolId protocol() const noexcept;

    /**
     * @brief Returns the ingress timestamp in nanoseconds since UNIX epoch.
     */
    [[nodiscard]] std::uint64_t timestampNs() const noexcept;

    /**
     * @brief Strongly-typed accessor for the metadata section.
     *
     * @tparam T Struct type to interpret the metadata section as (e.g. UdpMetadata).
     * @return const T* Pointer to the typed metadata struct, or nullptr if metadata length is insufficient.
     */
    template <typename T>
    [[nodiscard]] const T* getMetadata() const noexcept
    {
        if (m_metadata.size() < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(m_metadata.data());
    }

private:
    std::span<const std::byte> m_rawBuffer { };
    std::span<const std::byte> m_metadata { };
    std::span<const std::byte> m_payload { };
    const EnvelopeHeader* m_header { nullptr };
};

} // namespace Core::Message
