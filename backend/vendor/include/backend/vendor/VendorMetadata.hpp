#pragma once

#include <cstdint>

namespace Backend::Vendor {

/**
 * @brief Wire metadata struct embedded in the Proxy MessageEnvelope metadata section.
 *
 * Preserves the vendor unique message ID across handlers and transports so the backend
 * can cleanly look up the in-flight VendorMessage upon receiving onMessageResult().
 */
struct alignas(8) VendorMetadata {
    std::uint64_t messageId { 0 };
};

static_assert(sizeof(VendorMetadata) == 8, "VendorMetadata must be exactly 8 bytes");

} // namespace Backend::Vendor
