#pragma once

#include "backend/IOutputChannel.hpp"
#include "common/compat/Span.hpp"
#include <atomic>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

namespace Proxy::Backend::Uds {

/**
 * @brief Output channel for transmitting datagrams to a Unix Domain Socket path.
 */
class UdsOutputChannel : public ::Backend::IOutputChannel {
public:
    UdsOutputChannel(std::string name, std::string destPath);
    ~UdsOutputChannel() override;

    bool send(Common::Compat::span<const std::byte> message) override;
    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override;

private:
    std::string m_destPath;
    sockaddr_un m_destAddr { };
    int m_socketFd { -1 };
    std::atomic<bool> m_connected { false };

    void closeSocket();
    void openSocket();
};

} // namespace Proxy::Backend::Uds
