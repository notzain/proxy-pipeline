#pragma once

#include "backend/IInputChannel.hpp"
#include <filesystem>
#include <optional>

namespace Proxy::Backend::File {

/**
 * @brief Input channel for file-based backend watching a directory.
 */
class FileInputChannel : public ::Backend::IInputChannel {
public:
    explicit FileInputChannel(std::filesystem::path watchDir);
    ~FileInputChannel() override = default;

    std::optional<Buffer> receive(Timeout timeout) override;
    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;

private:
    std::filesystem::path m_watchDir;
};

} // namespace Proxy::Backend::File
