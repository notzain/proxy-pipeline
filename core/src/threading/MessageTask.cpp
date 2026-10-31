#include "core/threading/MessageTask.hpp"
#include <cstring>
#include <utility>
#include <vector>

namespace Core::Threading {

void MessageTask::assign(std::string_view name, Common::Compat::span<const std::byte> data)
{
    m_channelName = name;
    m_spillBuffer = std::monostate { };
    m_leasedView = { };
    m_leased = false;
    if (data.size() <= SmallBufferSize) {
        m_inlineSize = data.size();
        std::memcpy(m_inlineBuffer.data(), data.data(), m_inlineSize);
    } else {
        m_inlineSize = 0;
        m_spillBuffer = std::vector<std::byte>(data.begin(), data.end());
    }
}

void MessageTask::assignSlice(std::string_view name, Common::Memory::BufferSlice slice, std::size_t size)
{
    m_channelName = name;
    m_inlineSize = 0;
    m_leasedView = { };
    m_leased = false;
    slice.resize(size);
    m_spillBuffer = std::move(slice);
}

void MessageTask::assignLeased(std::string_view name, Common::Compat::span<const std::byte> view, Backend::Lease owner) noexcept
{
    if (m_channelName != name) {
        m_channelName = name;
    }
    m_inlineSize = 0;
    m_spillBuffer = std::monostate { };
    m_lease = std::move(owner);
    m_leasedView = view;
    m_leased = true;
}

MessageTask::MessageTask(std::string name, Common::Compat::span<const std::byte> data)
    : m_channelName(std::move(name))
{
    if (data.size() <= SmallBufferSize) {
        m_inlineSize = data.size();
        std::memcpy(m_inlineBuffer.data(), data.data(), m_inlineSize);
    } else {
        m_inlineSize = 0;
        m_spillBuffer = std::vector<std::byte>(data.begin(), data.end());
    }
}

MessageTask::MessageTask(MessageTask&& other) noexcept
    : m_channelName(std::move(other.m_channelName))
    , m_inlineSize(other.m_inlineSize)
    , m_spillBuffer(std::move(other.m_spillBuffer))
    , m_leasedView(std::exchange(other.m_leasedView, { }))
    , m_leased(std::exchange(other.m_leased, false))
    , m_lease(std::move(other.m_lease))
{
    if (m_inlineSize > 0) {
        std::memcpy(m_inlineBuffer.data(), other.m_inlineBuffer.data(), m_inlineSize);
    }
}

MessageTask& MessageTask::operator=(MessageTask&& other) noexcept
{
    if (this != &other) {
        m_channelName = std::move(other.m_channelName);
        m_inlineSize = other.m_inlineSize;
        m_spillBuffer = std::move(other.m_spillBuffer);
        m_leasedView = std::exchange(other.m_leasedView, { });
        m_leased = std::exchange(other.m_leased, false);
        m_lease = std::move(other.m_lease);
        if (m_inlineSize > 0) {
            std::memcpy(m_inlineBuffer.data(), other.m_inlineBuffer.data(), m_inlineSize);
        }
    }
    return *this;
}

std::string_view MessageTask::channelName() const noexcept
{
    return m_channelName;
}

Common::Compat::span<const std::byte> MessageTask::payload() const noexcept
{
    if (m_leased) {
        return m_leasedView;
    }
    if (m_inlineSize > 0) {
        return Common::Compat::span<const std::byte>(m_inlineBuffer.data(), m_inlineSize);
    }
    if (Common::Memory::hasData(m_spillBuffer)) {
        return Common::Memory::spanOf(m_spillBuffer);
    }
    return { };
}

Backend::PayloadFormat MessageTask::format() const noexcept
{
    return m_leased ? Backend::PayloadFormat::Raw : Backend::PayloadFormat::Enveloped;
}

void MessageTask::setLease(Backend::Lease lease) noexcept
{
    m_lease = std::move(lease);
}

Backend::Lease& MessageTask::lease() const noexcept
{
    return m_lease;
}

} // namespace Core::Threading
