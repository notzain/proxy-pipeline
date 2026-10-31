#pragma once

#include <cstdint>

namespace Backend {

/**
 * @brief Declares how the bytes of a received slot / task must be interpreted by the dispatcher.
 */
enum class PayloadFormat : std::uint8_t {
    /** Bytes are a native Proxy wire envelope and must be parsed via `MessageView::from()`. */
    Enveloped,
    /** Bytes are the bare application payload (no envelope header), e.g. leased vendor memory. */
    Raw,
};

} // namespace Backend
