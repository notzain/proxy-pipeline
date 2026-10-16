#pragma once

#include "backend/IBackend.hpp"
#include <cstdint>
#include <string>

namespace Proxy::Backend::Socket {

/**
 * @brief Socket-based transport backend implementing Backend::IBackend.
 *
 * Listens on a UDP socket configured through IConfig ("socket.host", "socket.port").
 * Emits incoming datagram packet payloads as messages.
 */
class SocketBackend : public ::Backend::IBackend {
public:
    /**
     * @brief Default constructor without parameters.
     */
    SocketBackend();

    ~SocketBackend() override;

    /**
     * @brief Configures socket parameters ("socket.host", "socket.port") and binds the UDP socket.
     *
     * @param config Strongly-typed key-value configuration.
     */
    void configure(const ::Backend::IConfig& config) override;

    /**
     * @brief Polls the UDP socket for incoming datagrams within the given timeout.
     *
     * @param timeout Maximum duration to wait for socket data.
     * @return std::optional<Buffer> Buffer containing received datagram payload, or std::nullopt.
     */
    std::optional<Buffer> nextMessage(Timeout timeout) override;

private:
    std::string m_host { "0.0.0.0" };
    std::uint16_t m_port { 8080 };
    int m_socketFd { -1 };

    void closeSocket();
    void openAndBindSocket();
};

} // namespace Proxy::Backend::Socket
