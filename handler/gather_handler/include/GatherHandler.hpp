#pragma once

#include "backend/channel/IInputChannel.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/InspectionHeader.hpp"
#include "backend/message/ProtocolId.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/handler/IHandler.hpp"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Pending assembly state for a scattered inspection packet.
 */
struct PendingGatherEntry {
    std::uint64_t correlationId { 0 };
    std::uint32_t receivedMask { 0 };
    std::uint32_t requiredMask { 0 };
    std::uint16_t totalSplits { 0 };
    Backend::ProtocolId protocol { Backend::RawProtocol };
    std::chrono::steady_clock::time_point ingressTime;
    std::vector<std::byte> originalMetadata;
    std::vector<std::byte> payload;
    bool hasRejection { false };
    std::uint32_t rejectionErrorCode { 0 };
};

/**
 * @brief GatherHandler aggregates parallel inspection splits and merges them back into a single packet.
 *
 * Verifies that all expected splits report successful verdicts before forwarding the packet
 * downstream to egress. Fast-drops packets on inspection rejection and evicts unmerged messages on timeout.
 */
class GatherHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~GatherHandler() override = default;

    void step() override;

    Backend::MessageResult onMessageReceived(
        std::string_view channelName,
        const Backend::MessageView& message) override;

    void setOutputChannel(Common::Memory::ObserverPtr<Backend::IOutputChannel> channel) { m_outputChannel = channel; }

protected:
    bool onConfigure(const Core::Config::Configuration& config) override;

private:
    void evictExpiredEntries();

    Common::Memory::ObserverPtr<Backend::IOutputChannel> m_outputChannel;
    std::vector<Common::Memory::ObserverPtr<Backend::IInputChannel>> m_inputChannelsList;

    std::mutex m_mutex;
    std::unordered_map<std::uint64_t, PendingGatherEntry> m_pending;
    std::chrono::milliseconds m_timeout { 5000 };
};

} // namespace Proxy::Handler
