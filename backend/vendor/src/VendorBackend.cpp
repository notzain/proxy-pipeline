#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "log/Log.hpp"
#include <cstdint>
#include <utility>

namespace Backend::Vendor {

VendorBackend::VendorBackend()
    : VendorBackend(std::make_shared<Sdk::VendorManager>())
{
}

VendorBackend::VendorBackend(
    std::shared_ptr<Sdk::VendorManager> manager,
    std::size_t poolBlockSize,
    std::size_t poolBlockCount)
    : m_vendorManager(std::move(manager))
    , m_poolBlockSize(poolBlockSize > 0 ? poolBlockSize : DefaultPoolBlockSize)
    , m_poolBlockCount(poolBlockCount > 0 ? poolBlockCount : DefaultPoolBlockCount)
{
    if (!m_vendorManager) {
        m_vendorManager = std::make_shared<Sdk::VendorManager>();
    }
}

VendorBackend::~VendorBackend() = default;

void VendorBackend::configure(const ::Backend::IConfig& config)
{
    const auto blockSize = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("vendor.pool_block_size", m_poolBlockSize));
    const auto blockCount = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("vendor.pool_block_count", m_poolBlockCount));

    if (m_bufferPool) {
        if (blockSize != m_poolBlockSize || blockCount != m_poolBlockCount) {
            Proxy::Log::warn("[VendorBackend] BufferPool already allocated ({} x {} B); ignoring reconfiguration.",
                m_poolBlockCount, m_poolBlockSize);
        }
        return;
    }

    m_poolBlockSize = blockSize > 0 ? blockSize : DefaultPoolBlockSize;
    m_poolBlockCount = blockCount > 0 ? blockCount : DefaultPoolBlockCount;
    Proxy::Log::info("[VendorBackend] Configured 3rd-party vendor backend (pool: {} x {} B).",
        m_poolBlockCount, m_poolBlockSize);
}

std::unique_ptr<::Backend::IInputChannel> VendorBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig& /*config*/)
{
    return std::make_unique<VendorInputChannel>(std::string(name), *this);
}

std::unique_ptr<::Backend::IOutputChannel> VendorBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig& /*config*/)
{
    return std::make_unique<VendorOutputChannel>(std::string(name), m_vendorManager);
}

std::shared_ptr<Sdk::VendorManager> VendorBackend::vendorManager() const noexcept
{
    return m_vendorManager;
}

std::shared_ptr<Common::Memory::BufferPool> VendorBackend::bufferPool()
{
    if (!m_bufferPool) {
        m_bufferPool = std::make_shared<Common::Memory::BufferPool>(m_poolBlockSize, m_poolBlockCount);
    }
    return m_bufferPool;
}

} // namespace Backend::Vendor

namespace Backend {

std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Vendor::VendorBackend>();
}

} // namespace Backend
