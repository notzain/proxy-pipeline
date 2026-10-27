#include "common/io/UniqueFd.hpp"
#include <unistd.h>

namespace Common::Io {

UniqueFd::UniqueFd(int fd) noexcept
    : m_fd(fd)
{
}

UniqueFd::~UniqueFd() noexcept
{
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept
    : m_fd(other.release())
{
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::get() const noexcept
{
    return m_fd;
}

bool UniqueFd::isValid() const noexcept
{
    return m_fd >= 0;
}

UniqueFd::operator bool() const noexcept
{
    return isValid();
}

int UniqueFd::release() noexcept
{
    int fd = m_fd;
    m_fd = -1;
    return fd;
}

void UniqueFd::reset(int newFd) noexcept
{
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    m_fd = newFd;
}

void UniqueFd::swap(UniqueFd& other) noexcept
{
    std::swap(m_fd, other.m_fd);
}

bool UniqueFd::operator==(const UniqueFd& other) const noexcept
{
    return m_fd == other.m_fd;
}

bool UniqueFd::operator!=(const UniqueFd& other) const noexcept
{
    return m_fd != other.m_fd;
}

} // namespace Common::Io
