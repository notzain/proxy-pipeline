#include "MessageDispatcher.hpp"
#include "backend/MessageView.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "log/Log.hpp"
#include <array>
#include <utility>

namespace Core::Threading::Detail {

void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    Common::Compat::span<const Common::Compat::span<const std::byte>> rawMessages,
    Common::Compat::span<::Backend::Lease> leases,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex)
{
    static auto& s_msgCounter = Telemetry::MetricsRegistry::instance().counter(
        "messages_ingress_total", "Total messages received at ingress");
    static auto& s_bytesCounter = Telemetry::MetricsRegistry::instance().counter(
        "bytes_ingress_total", "Total bytes received at ingress");
    static auto& s_droppedCounter = Telemetry::MetricsRegistry::instance().counter(
        "messages_dropped_total", "Total malformed messages dropped");

    std::array<Backend::MessageView, 16> views;
    std::array<Backend::Lease, 16> validLeases;
    const bool hasLeases = !leases.empty();
    std::size_t validViews = 0;
    std::size_t batchBytes = 0;

    for (std::size_t i = 0; i < rawMessages.size(); ++i) {
        const auto rawMessage = rawMessages[i];
        auto optView = Backend::MessageView::from(rawMessage);
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
            validLeases[validViews] = std::move(leases[i]);
        }
        views[validViews++] = *optView;
        batchBytes += rawMessage.size();
    }

    if (validViews == 0) {
        return;
    }

    s_msgCounter.increment(validViews);
    s_bytesCounter.increment(batchBytes);

    const auto messages = Common::Compat::span<const Backend::MessageView>(views.data(), validViews);
    const auto batchLeases = hasLeases
        ? Common::Compat::span<Backend::Lease>(validLeases.data(), validViews)
        : Common::Compat::span<Backend::Lease>();
    if (handlerMutex) {
        std::lock_guard lock(*handlerMutex);
        handler.processBatch(channelName, messages, batchLeases);
    } else {
        handler.processBatch(channelName, messages, batchLeases);
    }
}

} // namespace Core::Threading::Detail
