#pragma once

#include "backend/IInputChannel.hpp"
#include <filesystem>

namespace Proxy::Backend::File {

using ::Backend::BytesResult;

/**
 * @brief Input channel for file-based backend watching a directory.
 */
class FileInputChannel : public ::Backend::IInputChannel {
public:
    FileInputChannel(std::string name, std::filesystem::path watchDir);
    ~FileInputChannel() override = default;

    BytesResult receiveInto(std::span<std::byte> destination, Timeout timeout) override;

private:
    std::filesystem::path m_watchDir;
};

} // namespace Proxy::Backend::File
