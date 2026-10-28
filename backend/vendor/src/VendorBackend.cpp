#include "backend/vendor/VendorBackend.hpp"
#include "backend/MessageView.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorMetadata.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "log/Log.hpp"

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
    Proxy::Log::info("[VendorBackend] Configured 3rd-party vendor backend.");
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
    return std::make_unique<VendorOutputChannel>(std::string(name));
}

bool VendorBackend::tracksMessageResults() const noexcept
{
    return true;
}

void VendorBackend::onMessageResult(
    std::string_view channelName,
    Common::Compat::span<const std::byte> rawMessage,
    const ::Backend::MessageResult& result)
{
    auto view = ::Backend::MessageView::from(rawMessage);
    if (!view) {
        return;
    }

    const auto* meta = view->getMetadata<VendorMetadata>();
    if (!meta) {
        return;
    }
    const std::uint64_t msgId = meta->messageId;

    auto scopedMsg = unregisterInFlight(msgId);
    if (!scopedMsg) {
        Proxy::Log::warn("[VendorBackend] onMessageResult: In-flight message ID {} not found in registry", msgId);
        return;
    }

    if (result.is<::Backend::Dropped>()) {
        Proxy::Log::debug("[VendorBackend] Dropping message ID {} on channel '{}'", msgId, channelName);
        scopedMsg->drop();
    } else {
        Proxy::Log::debug("[VendorBackend] Finalizing message ID {} on channel '{}'", msgId, channelName);
        scopedMsg->finalize();
    }
}

void VendorBackend::registerInFlight(std::uint64_t msgId, ScopedVendorMessage msg)
{
    std::lock_guard lock(m_mutex);
    m_inFlightMessages.emplace(msgId, std::move(msg));
}

std::optional<ScopedVendorMessage> VendorBackend::unregisterInFlight(std::uint64_t msgId)
{
    std::lock_guard lock(m_mutex);
    auto it = m_inFlightMessages.find(msgId);
    if (it == m_inFlightMessages.end()) {
        return std::nullopt;
    }
    ScopedVendorMessage msg = std::move(it->second);
    m_inFlightMessages.erase(it);
    return msg;
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
