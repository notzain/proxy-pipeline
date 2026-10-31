#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "common/memory/BufferPool.hpp"
#include "log/Log.hpp"
#include <cstdint>
#include <utility>

namespace Backend::Vendor {

VendorBackend::VendorBackend()
    : m_vendorManager(std::make_shared<Sdk::VendorManager>())
{
}

VendorBackend::VendorBackend(
    std::shared_ptr<Sdk::VendorManager> manager,
    std::size_t poolBlockSize,
    std::size_t poolBlockCount)
    : m_vendorManager(std::move(manager))
{
    if (!m_vendorManager) {
        m_vendorManager = std::make_shared<Sdk::VendorManager>();
    }
    if (poolBlockSize != DefaultPoolBlockSize || poolBlockCount != DefaultPoolBlockCount) {
        setBufferPool(std::make_shared<Common::Memory::BufferPool>(
            poolBlockSize > 0 ? poolBlockSize : DefaultPoolBlockSize,
            poolBlockCount > 0 ? poolBlockCount : DefaultPoolBlockCount));
    }
}

VendorBackend::~VendorBackend() = default;

void VendorBackend::configure(const ::Backend::IConfig& config)
{
    // Mode B vendor messages arrive as file paths on disk rather than RAM pointers; this pool backs
    // the BufferSlices held inside VendorLeaseState when reading those files into memory.
    const auto curPool = bufferPool();
    const auto blockSize = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("vendor.pool_block_size",
            config.getOr<std::uint64_t>("memory.pool_block_size", curPool->blockSize())));
    const auto blockCount = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("vendor.pool_block_count",
            config.getOr<std::uint64_t>("memory.pool_block_count", curPool->blockCount())));

    if (blockSize != curPool->blockSize() || blockCount != curPool->blockCount()) {
        setBufferPool(std::make_shared<Common::Memory::BufferPool>(
            blockSize > 0 ? blockSize : DefaultPoolBlockSize,
            blockCount > 0 ? blockCount : DefaultPoolBlockCount));
    }

    const auto activePool = bufferPool();
    Proxy::Log::info("[VendorBackend] Configured 3rd-party vendor backend (pool: {} x {} B).",
        activePool->blockCount(), activePool->blockSize());
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

} // namespace Backend::Vendor

namespace Backend {

std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Vendor::VendorBackend>();
}

} // namespace Backend
