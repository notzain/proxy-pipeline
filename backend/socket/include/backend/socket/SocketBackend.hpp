#pragma once

#include "backend/IBackend.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Socket {

/**
 * @brief Socket-based transport backend implementing Backend::IBackend.
 *
 * Configures UDP transport settings and instantiates SocketInputChannel and SocketOutputChannel.
 */
class SocketBackend : public ::Backend::IBackend {
public:
    /**
     * @brief Default constructor without parameters.
     */
    SocketBackend();

    ~SocketBackend() override;

    /**
     * @brief Configures socket parameters and initializes socket logging.
     *
     * @param config Strongly-typed key-value configuration.
     */
    void configure(const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an input channel bound to configured host and port.
     */
    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view name, const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an output channel sending to configured destination host and port.
     */
    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view name, const ::Backend::IConfig& config) override;

private:
    std::string m_host { "0.0.0.0" };
    std::uint16_t m_port { 8080 };
};

} // namespace Proxy::Backend::Socket
