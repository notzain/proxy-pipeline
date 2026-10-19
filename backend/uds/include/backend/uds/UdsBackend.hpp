#pragma once

#include "backend/IBackend.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Uds {

/**
 * @brief Unix Domain Socket (UDS) transport backend implementing Backend::IBackend.
 *
 * Configures local IPC socket paths and instantiates UdsInputChannel and UdsOutputChannel.
 */
class UdsBackend : public ::Backend::IBackend {
public:
    UdsBackend();
    ~UdsBackend() override;

    /**
     * @brief Configures UDS parameters and initializes logging.
     *
     * @param config Strongly-typed key-value configuration.
     */
    void configure(const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an input channel bound to a local Unix Domain Socket path.
     */
    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view name, const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an output channel sending datagrams to a destination Unix Domain Socket path.
     */
    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view name, const ::Backend::IConfig& config) override;

private:
    std::string m_socketPath { "/tmp/proxy_uds.sock" };
};

} // namespace Proxy::Backend::Uds
