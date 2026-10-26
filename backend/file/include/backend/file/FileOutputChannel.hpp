#pragma once

#include "backend/IOutputChannel.hpp"
#include <filesystem>
#include <span>

namespace Proxy::Backend::File {

/**
 * @brief Output channel for file-based backend writing files to a directory.
 */
class FileOutputChannel : public ::Backend::IOutputChannel {
public:
    FileOutputChannel(std::string name, std::filesystem::path outputDir);
    ~FileOutputChannel() override = default;

    bool send(std::span<const std::byte> message) override;

private:
    std::filesystem::path m_outputDir;
};

} // namespace Proxy::Backend::File
