#pragma once

#include "backend/IOutputChannel.hpp"
#include <span>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

namespace Proxy::Backend::Uds {

/**
 * @brief Output channel for transmitting datagrams to a Unix Domain Socket path.
 */
class UdsOutputChannel : public ::Backend::IOutputChannel {
public:
    explicit UdsOutputChannel(std::string destPath);
    ~UdsOutputChannel() override;

    bool send(std::span<const std::byte> message) override;

private:
    std::string m_destPath;
    sockaddr_un m_destAddr { };
    int m_socketFd { -1 };

    void closeSocket();
    void openSocket();
};

} // namespace Proxy::Backend::Uds
