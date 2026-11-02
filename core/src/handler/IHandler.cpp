#include "core/handler/IHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/MessageView.hpp"
#include "common/compat/Time.hpp"
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

Common::Memory::Borrow<::Backend::IInputChannel> IHandler::bindInputChannel(
    std::string_view channelName,
    const Config::Configuration& config)
{
    auto channel = m_backend.createInputChannel(channelName, config);
    if (!channel) {
        Proxy::Log::error("[IHandler] Failed to create input channel '{}'", channelName);
        throw std::runtime_error(std::string("Failed to create input channel: ") + std::string(channelName));
    }
    return registerInputChannel(std::move(channel));
}

std::optional<Common::Memory::Borrow<::Backend::IInputChannel>> IHandler::tryBindInputChannel(
    std::string_view channelName,
    const Config::Configuration& config) noexcept
{
    try {
        auto channel = m_backend.createInputChannel(channelName, config);
        if (!channel) {
            Proxy::Log::error("[IHandler] Failed to create input channel '{}'", channelName);
            return std::nullopt;
        }
        return registerInputChannel(std::move(channel));
    } catch (const std::exception& e) {
        Proxy::Log::error("[IHandler] Exception creating input channel '{}': {}", channelName, e.what());
        return std::nullopt;
    }
}

Common::Memory::Borrow<::Backend::IOutputChannel> IHandler::bindOutputChannel(
    std::string_view channelName,
    const Config::Configuration& config)
{
    auto channel = m_backend.createOutputChannel(channelName, config);
    if (!channel) {
        Proxy::Log::error("[IHandler] Failed to create output channel '{}'", channelName);
        throw std::runtime_error(std::string("Failed to create output channel: ") + std::string(channelName));
    }
    return registerOutputChannel(std::move(channel));
}

std::optional<Common::Memory::Borrow<::Backend::IOutputChannel>> IHandler::tryBindOutputChannel(
    std::string_view channelName,
    const Config::Configuration& config) noexcept
{
    try {
        auto channel = m_backend.createOutputChannel(channelName, config);
        if (!channel) {
            Proxy::Log::error("[IHandler] Failed to create output channel '{}'", channelName);
            return std::nullopt;
        }
        return registerOutputChannel(std::move(channel));
    } catch (const std::exception& e) {
        Proxy::Log::error("[IHandler] Exception creating output channel '{}': {}", channelName, e.what());
        return std::nullopt;
    }
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
    const uint64_t nowNs = Common::Compat::toEpochNs(Common::Compat::nowUtc());
    for (const auto& message : messages) {
        if (message.timestampNs() > 0 && nowNs >= message.timestampNs()) {
            double durationUs = static_cast<double>(nowNs - message.timestampNs()) / 1000.0;
            latencyHist.observe(durationUs);
        }
    }

    Common::Memory::SmallSpanBuffer<::Backend::MessageResult, 64> resultsBuffer(messages.size());
    auto resultsSpan = resultsBuffer.span(messages.size());
    const auto settleCount = std::min(leases.size(), messages.size());
    onBatchReceived(channelName, messages, resultsSpan);

    if (m_backend.tracksMessageResults()) {
        for (std::size_t i = 0; i < messages.size(); ++i) {
            m_backend.onMessageResult(channelName, messages[i].rawBuffer(), resultsSpan[i]);
        }
    }

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
