#include "MessageDispatcher.hpp"
#include "core/message/MessageView.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "log/Log.hpp"
#include <array>

namespace Core::Threading::Detail {

void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    Common::Compat::span<const Common::Compat::span<const std::byte>> rawMessages,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex)
{
    static auto& s_msgCounter = Telemetry::MetricsRegistry::instance().counter(
        "messages_ingress_total", "Total messages received at ingress");
    static auto& s_bytesCounter = Telemetry::MetricsRegistry::instance().counter(
        "bytes_ingress_total", "Total bytes received at ingress");
    static auto& s_droppedCounter = Telemetry::MetricsRegistry::instance().counter(
        "messages_dropped_total", "Total malformed messages dropped");

    std::array<Message::MessageView, 16> views;
    std::size_t validViews = 0;
    std::size_t batchBytes = 0;

    for (const auto rawMessage : rawMessages) {
        auto optView = Message::MessageView::from(rawMessage);
        if (!optView) {
            ++invalidEnvelopeCount;
            s_droppedCounter.increment();
            if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                Proxy::Log::warn("[MessageDispatcher] Channel '{}' received invalid envelope ({} bytes, total dropped: {}).",
                    channelName, rawMessage.size(), invalidEnvelopeCount);
            }
            continue;
        }

        views[validViews++] = *optView;
        batchBytes += rawMessage.size();
    }

    if (validViews == 0) {
        return;
    }

    s_msgCounter.increment(validViews);
    s_bytesCounter.increment(batchBytes);

    const auto messages = Common::Compat::span<const Message::MessageView>(views.data(), validViews);
    if (handlerMutex) {
        std::lock_guard lock(*handlerMutex);
        handler.processBatch(channelName, messages);
    } else {
        handler.processBatch(channelName, messages);
    }
}

} // namespace Core::Threading::Detail
