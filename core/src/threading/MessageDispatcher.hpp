#pragma once

#include "backend/memory/Lease.hpp"
#include "backend/message/PayloadFormat.hpp"
#include "common/compat/Span.hpp"
#include "core/handler/IHandler.hpp"
#include <cstddef>
#include <mutex>
#include <string_view>

namespace Core::Threading::Detail {

/**
 * @brief Validates envelopes and dispatches a received batch to the handler, settling ownership leases.
 *
 * Leases of messages with invalid envelopes are rejected. Leases of valid messages are moved into
 * IHandler::processBatch(), which settles them from the handler results.
 *
 * @param handler Target handler.
 * @param channelName Name of the input channel.
 * @param rawMessages Received messages (enveloped frames or bare payloads, see @p formats).
 * @param formats Per-message payload format parallel to `rawMessages`, or empty if all are enveloped.
 *                `Raw` messages bypass envelope parsing and are wrapped directly into a MessageView.
 * @param leases Ownership leases parallel to `rawMessages`, or empty if the transport owns its buffers.
 * @param invalidEnvelopeCount Running counter of malformed envelopes for rate-limited logging.
 * @param handlerMutex Optional mutex serializing handler access.
 */
void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    Common::Compat::span<const Common::Compat::span<const std::byte>> rawMessages,
    Common::Compat::span<const ::Backend::PayloadFormat> formats,
    Common::Compat::span<::Backend::Lease> leases,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex);

} // namespace Core::Threading::Detail
