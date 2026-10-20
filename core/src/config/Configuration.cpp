#include "core/config/Configuration.hpp"
#include <fstream>
#include <string_view>

namespace Core::Config {

Configuration Configuration::fromFile(const std::filesystem::path& filePath)
{
    Configuration config;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        auto commentPos = line.find_first_of("#;");
        if (commentPos != std::string::npos) {
            line.erase(commentPos);
        }

        auto delimiterPos = line.find_first_of("=:");
        if (delimiterPos == std::string::npos) {
            continue;
        }

        auto rawKey = line.substr(0, delimiterPos);
        auto rawVal = line.substr(delimiterPos + 1);

        auto trim = [](std::string_view s) -> std::string {
            auto start = s.find_first_not_of(" \t\r\n");
            if (start == std::string_view::npos) {
                return "";
            }
            auto end = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(start, end - start + 1));
        };

        auto key = trim(rawKey);
        auto val = trim(rawVal);

        if (!key.empty()) {
            config.set(std::move(key), std::move(val));
        }
    }

    return config;
}

void Configuration::set(std::string key, std::string value)
{
    m_entries[std::move(key)] = std::move(value);
}

std::optional<std::string_view> Configuration::getString(std::string_view key) const
{
    if (auto it = m_entries.find(std::string(key)); it != m_entries.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool Configuration::has(std::string_view key) const noexcept
{
    return m_entries.contains(std::string(key));
}

} // namespace Core::Config
