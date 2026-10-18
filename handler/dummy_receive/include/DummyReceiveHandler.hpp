#pragma once

#include "core/IHandler.hpp"
#include <string>

namespace Proxy::Handler {

/**
 * @brief Test handler that registers an input channel and prints received messages.
 */
class DummyReceiveHandler : public Core::IHandler {
public:
    DummyReceiveHandler() = default;
    ~DummyReceiveHandler() override = default;

    /**
     * @brief Callback invoked when a message arrives on an input channel.
     *
     * @param channelName Name of the channel.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;
};

} // namespace Proxy::Handler
