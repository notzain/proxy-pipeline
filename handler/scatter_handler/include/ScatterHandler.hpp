#pragma once

#include "backend/channel/IOutputChannel.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/handler/IHandler.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief ScatterHandler dispatches incoming packets to M parallel inspection channels.
 *
 * Assigns a monotonic correlationId and packs an InspectionHeader onto each split
 * so downstream inspection branches can process concurrently.
 */
class ScatterHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~ScatterHandler() override = default;

    Backend::MessageResult onMessageReceived(
        std::string_view channelName,
        const Backend::MessageView& message) override;

    void addOutputChannel(Common::Memory::ObserverPtr<Backend::IOutputChannel> channel);

protected:
    bool onConfigure(const Core::Config::Configuration& config) override;

private:
    std::string m_outputChannelsStr;
    std::vector<Common::Memory::ObserverPtr<Backend::IOutputChannel>> m_outputChannelsList;
    std::atomic<std::uint64_t> m_nextCorrelationId { 1 };
};

} // namespace Proxy::Handler
