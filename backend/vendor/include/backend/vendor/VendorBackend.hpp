#pragma once

#include "backend/IBackend.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include "common/memory/BufferPool.hpp"
#include <cstddef>
#include <memory>

namespace Backend::Vendor {

/**
 * @brief 3rd-party vendor transport backend implementation.
 *
 * Manages the shared vendor SDK manager, creates vendor input/output channels, and
 * owns the shared BufferPool used when reading file-backed vendor payloads (Mode B).
 * The pool is sized from the constructor / `configure()` and allocated exactly once, lazily, when the
 * first channel needs it; later reconfiguration attempts are ignored with a warning.
 * In-flight vendor messages are owned directly by out-of-band Backend::Lease instances
 * produced by VendorInputChannel rather than a backend-wide registry.
 */
class VendorBackend : public ::Backend::IBackend {
public:
    static constexpr std::size_t DefaultPoolBlockSize = 65536;
    static constexpr std::size_t DefaultPoolBlockCount = 256;

    VendorBackend();
    explicit VendorBackend(
        std::shared_ptr<Sdk::VendorManager> manager,
        std::size_t poolBlockSize = DefaultPoolBlockSize,
        std::size_t poolBlockCount = DefaultPoolBlockCount);
    ~VendorBackend() override;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    [[nodiscard]] std::shared_ptr<Sdk::VendorManager> vendorManager() const noexcept;

    /**
     * @brief Returns the shared payload BufferPool, allocating it on first use with the configured geometry.
     */
    [[nodiscard]] std::shared_ptr<Common::Memory::BufferPool> bufferPool();

private:
    std::shared_ptr<Sdk::VendorManager> m_vendorManager;
    std::size_t m_poolBlockSize;
    std::size_t m_poolBlockCount;
    std::shared_ptr<Common::Memory::BufferPool> m_bufferPool;
};

} // namespace Backend::Vendor
