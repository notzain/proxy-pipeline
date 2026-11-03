#include "TextCondition.hpp"
#include "common/compat/StringView.hpp"
#include <string_view>

namespace Proxy::Handler {

TextCondition::TextCondition(std::string pattern, TextMatchMode mode, std::size_t minTimes)
    : m_pattern(std::move(pattern))
    , m_mode(mode)
    , m_minTimes(minTimes)
{
}

bool TextCondition::matches(const Backend::MessageView& message) const noexcept
{
    auto payload = message.payload();
    std::string_view payloadStr(reinterpret_cast<const char*>(payload.data()), payload.size());

    if (m_pattern.empty()) {
        return true;
    }

    if (m_mode == TextMatchMode::Exact) {
        return payloadStr == m_pattern;
    }

    if (m_mode == TextMatchMode::Prefix) {
        return Common::Compat::starts_with(payloadStr, m_pattern);
    }

    // TextMatchMode::Contains with times/count
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = payloadStr.find(m_pattern, pos)) != std::string_view::npos) {
        ++count;
        if (count >= m_minTimes) {
            return true;
        }
        pos += m_pattern.size();
    }

    return count >= m_minTimes;
}

std::unique_ptr<IRouteCondition> TextCondition::clone() const
{
    return std::make_unique<TextCondition>(*this);
}

const std::string& TextCondition::pattern() const noexcept
{
    return m_pattern;
}

TextMatchMode TextCondition::mode() const noexcept
{
    return m_mode;
}

std::size_t TextCondition::minTimes() const noexcept
{
    return m_minTimes;
}

TextCondition& TextCondition::times(std::size_t count) noexcept
{
    m_minTimes = count;
    return *this;
}

} // namespace Proxy::Handler
