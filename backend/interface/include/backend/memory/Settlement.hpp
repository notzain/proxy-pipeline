#pragma once

#include <cstdint>

namespace Backend {

/**
 * @brief Terminal outcome reported to a transport that owns an in-flight message.
 */
enum class Settlement : std::uint8_t {
    Ack, ///< Message fully processed; the transport may forget it (e.g. vendor finalize).
    Reject, ///< Message intentionally discarded / poison; must not be redelivered (e.g. vendor drop).
    Release ///< Message not processed; return it to the transport for redelivery.
};

} // namespace Backend
