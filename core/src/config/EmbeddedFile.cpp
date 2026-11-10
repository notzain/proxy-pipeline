#include "core/config/EmbeddedFile.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <fstream>
#include <sstream>

namespace Core::Config {

EmbeddedFile::EmbeddedFile(std::string content)
    : m_content(std::move(content))
    , m_isEmbedded(true)
    , m_loaded(true)
{
}

EmbeddedFile::EmbeddedFile(std::filesystem::path path)
    : m_path(std::move(path))
    , m_isEmbedded(false)
    , m_loaded(false)
{
}

EmbeddedFile EmbeddedFile::fromContent(std::string content)
{
    return EmbeddedFile(std::move(content));
}

EmbeddedFile EmbeddedFile::fromPath(std::filesystem::path filePath)
{
    return EmbeddedFile(std::move(filePath));
}

std::optional<EmbeddedFile> EmbeddedFile::fromConfig(const Configuration& config, std::string_view keyPrefix)
{
    std::string prefix(keyPrefix);

    // 1. Check embedded content keys: <prefix>_content or <prefix>.content
    std::string contentKey1 = prefix + "_content";
    if (auto c1 = config.get<std::string>(contentKey1); c1.has_value() && !c1->empty()) {
        return EmbeddedFile::fromContent(std::move(*c1));
    }
    std::string contentKey2 = prefix + ".content";
    if (auto c2 = config.get<std::string>(contentKey2); c2.has_value() && !c2->empty()) {
        return EmbeddedFile::fromContent(std::move(*c2));
    }

    // 2. Check path keys: <prefix>_path, <prefix>.path, or <prefix>
    std::string pathKey1 = prefix + "_path";
    if (auto p1 = config.get<std::string>(pathKey1); p1.has_value() && !p1->empty()) {
        return EmbeddedFile::fromPath(std::filesystem::path(std::move(*p1)));
    }
    std::string pathKey2 = prefix + ".path";
    if (auto p2 = config.get<std::string>(pathKey2); p2.has_value() && !p2->empty()) {
        return EmbeddedFile::fromPath(std::filesystem::path(std::move(*p2)));
    }
    if (auto p3 = config.get<std::string>(prefix); p3.has_value() && !p3->empty()) {
        return EmbeddedFile::fromPath(std::filesystem::path(std::move(*p3)));
    }

    return std::nullopt;
}

bool EmbeddedFile::isEmbedded() const noexcept
{
    return m_isEmbedded;
}

const std::filesystem::path& EmbeddedFile::path() const noexcept
{
    return m_path;
}

std::string_view EmbeddedFile::content() const
{
    if (m_loaded) {
        return m_content;
    }

    if (m_path.empty()) {
        return { };
    }

    std::ifstream file(m_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        PROXY_LOG_ERROR("Failed to open path: '{filePath}'", m_path.string());
        return { };
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    m_content = ss.str();
    m_loaded = true;
    return m_content;
}

bool EmbeddedFile::isValid() const
{
    return !content().empty();
}

} // namespace Core::Config
