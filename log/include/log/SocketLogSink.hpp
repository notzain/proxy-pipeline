#pragma once

#include "log/ILogSink.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace Proxy::Log {

/**
 * @brief Log sink that transmits log datagrams to a remote UDP socket.
 */
class SocketLogSink : public ILogSink {
public:
    /**
     * @brief Constructs a new SocketLogSink.
     *
     * @param host Destination host or IP address.
     * @param port Destination UDP port.
     * @param alsoLogToConsole Whether to mirror log entries to console.
     */
    SocketLogSink(std::string host, std::uint16_t port, bool alsoLogToConsole = true);

    /**
     * @brief Destructor.
     */
    ~SocketLogSink() override;

    SocketLogSink(const SocketLogSink&) = delete;
    SocketLogSink& operator=(const SocketLogSink&) = delete;
    SocketLogSink(SocketLogSink&&) noexcept;
    SocketLogSink& operator=(SocketLogSink&&) noexcept;

    void log(Level level, std::string_view message) override;
    void flush() override;
    void setLevel(Level level) override;
    [[nodiscard]] Level getLevel() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Log
