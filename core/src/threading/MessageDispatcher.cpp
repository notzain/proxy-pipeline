#include "MessageDispatcher.hpp"
#include "backend/message/MessageView.hpp"
#include "common/memory/ParallelArray.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "log/Log.hpp"
#include <optional>
#include <utility>

namespace Core::Threading::Detail {

void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    Common::Compat::span<const Common::Compat::span<const std::byte>> rawMessages,
    Common::Compat::span<const ::Backend::PayloadFormat> formats,
    Common::Compat::span<::Backend::Lease> leases,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex)
{
    auto& registry = Telemetry::MetricsRegistry::instance();
    auto& s_msgCounter = registry.counter(
        "messages_ingress_total", "Total messages received at ingress");
    auto& s_bytesCounter = registry.counter(
        "bytes_ingress_total", "Total bytes received at ingress");
    auto& s_droppedCounter = registry.counter(
        "messages_dropped_total", "Total malformed messages dropped");

    Common::Memory::ParallelArray<16, Backend::MessageView, Backend::Lease> validBatch;
    const bool hasLeases = !leases.empty();
    std::size_t batchBytes = 0;

    for (std::size_t i = 0; i < rawMessages.size(); ++i) {
        const auto rawMessage = rawMessages[i];
        const bool isRaw = !formats.empty() && formats[i] == Backend::PayloadFormat::Raw;
        // Raw payloads (e.g. leased vendor memory) carry no wire header and are never parsed as envelopes,
        // so a payload that happens to resemble a header cannot be misinterpreted.
        auto optView = isRaw
            ? std::optional<Backend::MessageView>(Backend::MessageView(rawMessage))
            : Backend::MessageView::from(rawMessage);
        if (!optView) {
            if (hasLeases) {
                leases[i].reject();
            }
            ++invalidEnvelopeCount;
            s_droppedCounter.increment();
            if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                Proxy::Log::warn("[MessageDispatcher] Channel '{}' received invalid envelope ({} bytes, total dropped: {}).",
                    channelName, rawMessage.size(), invalidEnvelopeCount);
            }
            continue;
        }

        if (hasLeases) {
            validBatch.push_back(*optView, std::move(leases[i]));
            validBatch.get<0>(validBatch.size() - 1).attachLease(validBatch.get<1>(validBatch.size() - 1));
        } else {
            validBatch.push_back(*optView, Backend::Lease { });
        }
        batchBytes += rawMessage.size();
    }

    if (validBatch.empty()) {
        return;
    }

    s_msgCounter.increment(validBatch.size());
    s_bytesCounter.increment(batchBytes);

    const auto messages = validBatch.span<0>();
    const auto batchLeases = hasLeases
        ? validBatch.span<1>()
        : Common::Compat::span<Backend::Lease>();
    if (handlerMutex) {
        std::lock_guard lock(*handlerMutex);
        handler.processBatch(channelName, messages, batchLeases);
    } else {
        handler.processBatch(channelName, messages, batchLeases);
    }
}

} // namespace Core::Threading::Detail
