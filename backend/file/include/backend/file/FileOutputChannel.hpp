#pragma once

#include "backend/channel/IOutputChannel.hpp"
#include "common/compat/Span.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>

namespace Proxy::Backend::File {

/**
 * @brief Output channel for file-based backend writing files to a directory.
 */
class FileOutputChannel : public ::Backend::IOutputChannel {
public:
    FileOutputChannel(std::string name, std::filesystem::path outputDir);
    ~FileOutputChannel() override = default;

    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override;

private:
    std::filesystem::path m_outputDir;
    std::atomic<std::uint64_t> m_writeFailureCount { 0 };
};

} // namespace Proxy::Backend::File
