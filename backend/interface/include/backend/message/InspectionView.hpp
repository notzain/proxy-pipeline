#pragma once

#include "backend/message/InspectionHeader.hpp"
#include "backend/message/MessageView.hpp"
#include "common/compat/Span.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <vector>

namespace Backend {

/**
 * @brief Zero-copy, non-owning view wrapper around an in-flight inspection message.
 *
 * Inspects the metadata of a MessageView for a valid InspectionHeader.
 * Provides extractHeader() and extractMetadata() to access the tunneled payload,
 * and a static pack() helper to produce an inspection envelope wrapping a MessageView.
 */
class InspectionView {
public:
    /**
     * @brief Creates an InspectionView if the MessageView contains a valid InspectionHeader.
     *
     * @param message Message view to inspect.
     * @return std::optional<InspectionView> Valid view if magic matches and split indices are valid, else std::nullopt.
     */
    static std::optional<InspectionView> from(const MessageView& message) noexcept;

    /**
     * @brief Creates a new serialized binary envelope wrapping @p message with an InspectionHeader.
     *
     * Prepends the InspectionHeader into the metadata section while preserving the message's original
     * protocol, timestamp, and application payload.
     *
     * @param message The original message view to wrap.
     * @param header The InspectionHeader to prepend.
     * @return std::optional<std::vector<std::byte>> The serialized envelope, or std::nullopt if it exceeds wire limits.
     * @throws std::bad_alloc If envelope storage cannot be allocated.
     */
    static std::optional<std::vector<std::byte>> pack(const MessageView& message, const InspectionHeader& header);

    /**
     * @brief Returns the underlying MessageView.
     */
    [[nodiscard]] const MessageView& messageView() const noexcept { return m_message; }

    /**
     * @brief Returns the parsed InspectionHeader by value.
     */
    [[nodiscard]] InspectionHeader extractHeader() const noexcept { return m_header; }

    /**
     * @brief Returns a non-owning span of the original tunneled transport/customer metadata.
     */
    [[nodiscard]] Common::Compat::span<const std::byte> extractMetadata() const noexcept;

    /**
     * @brief Deserializes original strongly-typed metadata struct `T` if present and matching type size.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> extractMetadataAs() const noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "Metadata type T must be trivially copyable");
        const auto metaSpan = extractMetadata();
        if (metaSpan.size() < sizeof(T)) {
            return std::nullopt;
        }
        T out { };
        std::memcpy(&out, metaSpan.data(), sizeof(T));
        return out;
    }

private:
    explicit InspectionView(const MessageView& message, const InspectionHeader& header) noexcept
        : m_message(message)
        , m_header(header)
    {
    }

    MessageView m_message;
    InspectionHeader m_header;
};

} // namespace Backend
