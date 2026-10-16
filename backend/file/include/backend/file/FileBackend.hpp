#pragma once

#include "backend/IBackend.hpp"
#include <filesystem>
#include <string>

namespace Proxy::Backend::File {

/**
 * @brief File-based transport backend implementing Backend::IBackend.
 *
 * Implements a file watcher over the configured directory path ("file.path").
 * When a new file is detected, it is read into memory, passed as a message,
 * and then deleted immediately so no state tracking across runs is required.
 */
class FileBackend : public ::Backend::IBackend {
public:
    /**
     * @brief Default constructor without parameters.
     */
    FileBackend() = default;

    ~FileBackend() override = default;

    /**
     * @brief Configures the directory path to watch ("file.path").
     *
     * @param config Strongly-typed key-value configuration.
     */
    void configure(const ::Backend::IConfig& config) override;

    /**
     * @brief Polls the watched directory for any new file within the timeout.
     * Reads the file contents and deletes the file before returning.
     *
     * @param timeout Maximum wait duration.
     * @return std::optional<Buffer> Buffer containing the read file's content, or std::nullopt.
     */
    std::optional<Buffer> nextMessage(Timeout timeout) override;

private:
    std::filesystem::path m_watchDirectory { "./watch" };
};

} // namespace Proxy::Backend::File
