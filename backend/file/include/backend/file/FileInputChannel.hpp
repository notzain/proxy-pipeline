#pragma once

#include "backend/channel/IInputChannel.hpp"
#include <filesystem>

namespace Proxy::Backend::File {

using Common::Net::BytesResult;

/**
 * @brief Input channel for file-based backend watching a directory.
 */
class FileInputChannel : public ::Backend::IInputChannel {
public:
    FileInputChannel(std::string name, std::filesystem::path watchDir);
    ~FileInputChannel() override = default;

    BytesResult receiveSlot(::Backend::BufferSlot& slot, Timeout timeout) override;

private:
    std::filesystem::path m_watchDir;
};

} // namespace Proxy::Backend::File
