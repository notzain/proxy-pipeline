#include "backend/inproc/InProcBackend.hpp"
#include "InProcInputChannel.hpp"
#include "InProcOutputChannel.hpp"
#include "InProcRing.hpp"
#include <mutex>
#include <unordered_map>

namespace Proxy::Backend::InProc {

struct InProcBackend::Impl {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<InProcRing>> rings;
};

InProcBackend::InProcBackend()
    : m_impl(std::make_unique<Impl>())
{
}

InProcBackend::~InProcBackend() = default;

InProcBackend::InProcBackend(InProcBackend&&) noexcept = default;
InProcBackend& InProcBackend::operator=(InProcBackend&&) noexcept = default;

void InProcBackend::configure(const ::Backend::IConfig&)
{
}

std::shared_ptr<InProcRing> InProcBackend::getOrCreateRing(std::string_view name)
{
    std::lock_guard lock(m_impl->mutex);
    auto it = m_impl->rings.find(std::string(name));
    if (it != m_impl->rings.end()) {
        return it->second;
    }
    auto ring = std::make_shared<InProcRing>();
    m_impl->rings.emplace(std::string(name), ring);
    return ring;
}

std::unique_ptr<::Backend::IInputChannel> InProcBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig&)
{
    auto ring = getOrCreateRing(name);
    return std::make_unique<InProcInputChannel>(std::string(name), std::move(ring));
}

std::unique_ptr<::Backend::IOutputChannel> InProcBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig&)
{
    auto ring = getOrCreateRing(name);
    return std::make_unique<InProcOutputChannel>(std::string(name), std::move(ring));
}

void InProcBackend::clear()
{
    std::lock_guard lock(m_impl->mutex);
    for (auto& [_, ring] : m_impl->rings) {
        if (ring) {
            ring->stop();
        }
    }
    m_impl->rings.clear();
}

std::unique_ptr<::Backend::IBackend> createBackend()
{
    return std::make_unique<InProcBackend>();
}

} // namespace Proxy::Backend::InProc
