#pragma once

#include "backend/IBackend.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Shm {

/**
 * @brief Shared Memory (SHM) transport backend implementing Backend::IBackend.
 *
 * Configures local shared memory paths and instantiates ShmInputChannel and ShmOutputChannel
 * for kernel-bypass inter-process communication.
 */
class ShmBackend : public ::Backend::IBackend {
public:
    ShmBackend();
    ~ShmBackend() override;

    /**
     * @brief Configures SHM parameters and initializes logging.
     *
     * @param config Strongly-typed key-value configuration.
     */
    void configure(const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an input channel reading from a shared memory ring buffer.
     */
    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view name, const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an output channel writing to a shared memory ring buffer.
     */
    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view name, const ::Backend::IConfig& config) override;

private:
    std::string m_shmPath { "/tmp/proxy_shm.dat" };
    std::size_t m_capacity { 1024 };
};

} // namespace Proxy::Backend::Shm
