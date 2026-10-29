#include "core/handler/IHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/MessageView.hpp"
#include "common/memory/Borrow.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace Core::Handler {

IHandler::IHandler(::Backend::IBackend& backend) noexcept
    : m_backend(backend)
{
}

IHandler::~IHandler() = default;

bool IHandler::configure(const Config::Configuration& config)
{
    return onConfigure(config);
}

Common::Memory::Borrow<::Backend::IInputChannel> IHandler::registerInputChannel(std::unique_ptr<::Backend::IInputChannel> channel)
{
    if (!channel) {
        Proxy::Log::error("[IHandler] Attempted to register null input channel");
        throw std::invalid_argument("Attempted to register null input channel");
    }
    auto* rawPtr = channel.get();
    m_inputChannels.push_back(std::move(channel));
    return Common::Memory::Borrow<::Backend::IInputChannel>(*rawPtr);
}

std::size_t IHandler::inputChannelCount() const noexcept
{
    return m_inputChannels.size();
}

::Backend::MessageResult IHandler::onMessageReceived(std::string_view, const Backend::MessageView&)
{
    return ::Backend::Consumed { };
}

void IHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<::Backend::MessageResult> outResults)
{
    const auto count = std::min(messages.size(), outResults.size());
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = onMessageReceived(channelName, messages[i]);
    }
}

void IHandler::processBatch(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<::Backend::Lease> leases)
{
    auto& latencyHist = Telemetry::MetricsRegistry::instance().latencyHistogram(
        "message_processing_duration_us", "Message transit and processing duration in microseconds");
    uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
            .count());
    for (const auto& message : messages) {
        if (message.timestampNs() > 0 && nowNs >= message.timestampNs()) {
            double durationUs = static_cast<double>(nowNs - message.timestampNs()) / 1000.0;
            latencyHist.observe(durationUs);
        }
    }

    Common::Memory::SmallSpanBuffer<::Backend::MessageResult, 64> resultsBuffer(messages.size());
    auto resultsSpan = resultsBuffer.span(messages.size());
    onBatchReceived(channelName, messages, resultsSpan);

    if (m_backend.tracksMessageResults()) {
        for (std::size_t i = 0; i < messages.size(); ++i) {
            m_backend.onMessageResult(channelName, messages[i].rawBuffer(), resultsSpan[i]);
        }
    }

    const auto settleCount = std::min(leases.size(), messages.size());
    for (std::size_t i = 0; i < settleCount; ++i) {
        leases[i].settle(resultsSpan[i].is<::Backend::Dropped>() ? ::Backend::Settlement::Reject : ::Backend::Settlement::Ack);
    }
}

::Backend::IBackend& IHandler::backend() noexcept
{
    return m_backend;
}

Common::Memory::Borrow<::Backend::IOutputChannel> IHandler::registerOutputChannel(std::unique_ptr<::Backend::IOutputChannel> channel)
{
    if (!channel) {
        Proxy::Log::error("[IHandler] Attempted to register null output channel");
        throw std::invalid_argument("Attempted to register null output channel");
    }
    auto* rawPtr = channel.get();
    m_outputChannels.push_back(std::move(channel));
    return Common::Memory::Borrow<::Backend::IOutputChannel>(*rawPtr);
}

void IHandler::step()
{
}

} // namespace Core::Handler
