#include "core/MessageTask.hpp"
#include <algorithm>

namespace Core {

void MessageTask::assign(std::string_view name, std::span<const std::byte> data)
{
    m_channelName = name;
    if (data.size() <= SmallBufferSize) {
        m_inlineSize = data.size();
        m_heapBuffer.clear();
        std::memcpy(m_inlineBuffer.data(), data.data(), m_inlineSize);
    } else {
        m_inlineSize = 0;
        m_heapBuffer.assign(data.begin(), data.end());
    }
}

std::span<std::byte> MessageTask::prepareInline(std::string_view name)
{
    if (m_channelName != name) {
        m_channelName = name;
    }
    return std::span<std::byte>(m_inlineBuffer.data(), m_inlineBuffer.size());
}

void MessageTask::commitInline(std::size_t size) noexcept
{
    m_inlineSize = std::min(size, SmallBufferSize);
    m_heapBuffer.clear();
}

MessageTask::MessageTask(std::string name, std::span<const std::byte> data)
    : m_channelName(std::move(name))
{
    if (data.size() <= SmallBufferSize) {
        m_inlineSize = data.size();
        std::memcpy(m_inlineBuffer.data(), data.data(), m_inlineSize);
    } else {
        m_inlineSize = 0;
        m_heapBuffer.assign(data.begin(), data.end());
    }
}

MessageTask::MessageTask(MessageTask&& other) noexcept
    : m_channelName(std::move(other.m_channelName))
    , m_inlineSize(other.m_inlineSize)
    , m_heapBuffer(std::move(other.m_heapBuffer))
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
        m_heapBuffer = std::move(other.m_heapBuffer);
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

std::span<const std::byte> MessageTask::payload() const noexcept
{
    if (m_inlineSize > 0) {
        return std::span<const std::byte>(m_inlineBuffer.data(), m_inlineSize);
    }
    return std::span<const std::byte>(m_heapBuffer.data(), m_heapBuffer.size());
}

} // namespace Core
