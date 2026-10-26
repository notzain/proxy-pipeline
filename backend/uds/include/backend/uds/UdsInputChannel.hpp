#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include <memory>
#include <optional>
#include <string>

namespace Proxy::Backend::Uds {

/**
 * @brief Input channel for receiving datagrams on a bound Unix Domain Socket.
 */
class UdsInputChannel : public ::Backend::IInputChannel {
public:
    UdsInputChannel(std::string name, std::string socketPath);
    ~UdsInputChannel() override;

    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;
    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout) override;

private:
    std::string m_socketPath;
    int m_socketFd { -1 };
    std::unique_ptr<::Backend::ISocketPoller> m_poller;

    void closeSocket();
    void openAndBindSocket();
};

} // namespace Proxy::Backend::Uds
