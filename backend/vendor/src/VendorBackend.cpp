#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "log/Log.hpp"
#include <cstdint>
#include <utility>

namespace Backend::Vendor {

VendorBackend::VendorBackend()
    : m_vendorManager(std::make_shared<Sdk::VendorManager>())
{
}

VendorBackend::VendorBackend(std::shared_ptr<Sdk::VendorManager> manager)
    : m_vendorManager(std::move(manager))
{
    if (!m_vendorManager) {
        m_vendorManager = std::make_shared<Sdk::VendorManager>();
    }
}

VendorBackend::~VendorBackend() = default;

void VendorBackend::configure(const ::Backend::IConfig& /*config*/)
{
    PROXY_LOG_INFO("Configured 3rd-party vendor backend.");
}

std::unique_ptr<::Backend::IInputChannel> VendorBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig& config)
{
    const auto capacity = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("vendor.relay_capacity", VendorInputChannel::DefaultCapacity));
    return std::make_unique<VendorInputChannel>(std::string(name), *this, nullptr, capacity);
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
