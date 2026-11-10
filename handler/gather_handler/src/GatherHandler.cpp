#include "GatherHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/InspectionHeader.hpp"
#include "backend/message/InspectionView.hpp"
#include "backend/message/MessageView.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> splitString(std::string_view str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream { std::string(str) };
    while (std::getline(tokenStream, token, delimiter)) {
        auto start = token.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            auto end = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}
} // namespace

namespace Proxy::Handler {

bool GatherHandler::onConfigure(const Core::Config::Configuration& config)
{
    std::string outChannelName = config.getOr<std::string>("gather.output_channel", "gather_out");
    auto optOutput = tryBindOutputChannel(outChannelName, config);
    if (!optOutput) {
        return false;
    }
    m_outputChannel = optOutput.value();

    std::string inChannelsStr = config.getOr<std::string>("gather.input_channels", "");
    std::vector<std::string> inChannelNames = splitString(inChannelsStr, ',');
    if (inChannelNames.empty()) {
        PROXY_LOG_ERROR("No input channels specified in 'gather.input_channels'");
        return false;
    }

    for (const auto& inName : inChannelNames) {
        auto optInput = tryBindInputChannel(inName, config);
        if (!optInput) {
            return false;
        }
        m_inputChannelsList.push_back(optInput.value());
    }

    auto timeoutMs = config.getOr<std::int64_t>("gather.timeout_ms", 5000);
    m_timeout = std::chrono::milliseconds(timeoutMs);

    PROXY_LOG_INFO("Configured with {} input channels, timeout={}ms",
        m_inputChannelsList.size(), timeoutMs);
    return true;
}

void GatherHandler::step()
{
    evictExpiredEntries();
}

void GatherHandler::evictExpiredEntries()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now - it->second.ingressTime > m_timeout) {
            PROXY_LOG_WARN("Message correlationId={} timed out waiting for splits ({}/{} received); discarding",
                it->first, __builtin_popcount(it->second.receivedMask), it->second.totalSplits);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

Backend::MessageResult GatherHandler::onMessageReceived(
    std::string_view channelName,
    const Backend::MessageView& message)
{
    auto inspViewOpt = Backend::InspectionView::from(message);
    if (!inspViewOpt.has_value()) {
        PROXY_LOG_WARN("Received message on '{}' without valid InspectionHeader (protocol={:#x}); passing raw or dropping",
            channelName, static_cast<unsigned>(message.protocol().value()));
        return Backend::Dropped { .reason = "Missing valid InspectionHeader", .errorCode = 400 };
    }

    const auto& inspView = *inspViewOpt;
    const auto header = inspView.extractHeader();
    const auto corrId = header.correlationId;
    const auto splitIdx = header.splitIndex;
    const auto totalSplits = header.totalSplits;

    PROXY_LOG_DEBUG("Received split {}/{} for correlationId={} on channel '{}' with verdict={}",
        splitIdx, totalSplits, corrId, channelName, static_cast<int>(header.verdict));

    std::vector<std::byte> completedPayload;
    std::vector<std::byte> completedMetadata;
    Backend::ProtocolId originalProtocol { Backend::RawProtocol };
    bool shouldForward = false;
    bool shouldDropFast = false;
    std::string dropReason;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_pending.find(corrId);
        if (it == m_pending.end()) {
            if (header.verdict == Backend::InspectionVerdict::Reject || header.verdict == Backend::InspectionVerdict::Error) {
                PROXY_LOG_WARN("Early rejection for correlationId={} on split {}", corrId, splitIdx);
                return Backend::Dropped { .reason = "Inspection split reported rejection", .errorCode = static_cast<int>(header.errorCode) };
            }

            auto origMetaSpan = inspView.extractMetadata();

            PendingGatherEntry entry {
                .correlationId = corrId,
                .receivedMask = 0,
                .requiredMask = (totalSplits >= 32) ? 0xFFFFFFFF : ((1u << totalSplits) - 1),
                .totalSplits = totalSplits,
                .protocol = message.protocol(), // Preserve original untouched protocol!
                .ingressTime = std::chrono::steady_clock::now(),
                .originalMetadata = std::vector<std::byte>(origMetaSpan.begin(), origMetaSpan.end()),
                .payload = std::vector<std::byte>(message.payload().begin(), message.payload().end()),
                .hasRejection = false,
                .rejectionErrorCode = 0
            };
            it = m_pending.emplace(corrId, std::move(entry)).first;
        }

        auto& entry = it->second;

        if (header.verdict == Backend::InspectionVerdict::Reject || header.verdict == Backend::InspectionVerdict::Error) {
            entry.hasRejection = true;
            entry.rejectionErrorCode = header.errorCode;
            shouldDropFast = true;
            dropReason = "Inspection split reported rejection";
            m_pending.erase(it);
        } else {
            entry.receivedMask |= (1u << splitIdx);
            if (entry.receivedMask == entry.requiredMask) {
                shouldForward = true;
                completedPayload = std::move(entry.payload);
                completedMetadata = std::move(entry.originalMetadata);
                originalProtocol = entry.protocol;
                m_pending.erase(it);
            }
        }
    }

    if (shouldDropFast) {
        PROXY_LOG_WARN("Dropping correlationId={} due to inspector failure/rejection", corrId);
        return Backend::Dropped { .reason = dropReason, .errorCode = static_cast<int>(header.errorCode) };
    }

    if (shouldForward) {
        if (!m_outputChannel) {
            PROXY_LOG_ERROR("No output channel configured to forward gathered packet");
            return Backend::Dropped { .reason = "No output channel configured", .errorCode = 500 };
        }

        PROXY_LOG_INFO("All {} splits gathered for correlationId={} (restoring protocol={}, {} meta bytes); forwarding to '{}'",
            totalSplits, corrId, static_cast<unsigned>(originalProtocol.value()), completedMetadata.size(), m_outputChannel->name());

        const auto restoredMsg = Backend::MessageView::pack(
            originalProtocol,
            Common::Compat::span<const std::byte>(completedMetadata.data(), completedMetadata.size()),
            Common::Compat::span<const std::byte>(completedPayload.data(), completedPayload.size()),
            message.timestampNs());
        if (!restoredMsg) {
            PROXY_LOG_WARN("Could not restore gathered envelope: message exceeds envelope size limit");
            return Backend::Dropped { .reason = "Gathered message exceeds envelope size limit", .errorCode = 413 };
        }

        auto optView = Backend::MessageView::from(Common::Compat::span<const std::byte>(restoredMsg->data(), restoredMsg->size()));
        if (optView && m_outputChannel->send(*optView)) {
            return Backend::Forwarded { .channel = m_outputChannel->name() };
        }

        PROXY_LOG_WARN("Failed to forward gathered message to '{}'", m_outputChannel->name());
        return Backend::Dropped { .reason = "Failed to send to output channel", .errorCode = 500 };
    }

    return Backend::Consumed { };
}

} // namespace Proxy::Handler
