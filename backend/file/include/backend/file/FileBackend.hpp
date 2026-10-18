#pragma once

#include "backend/IBackend.hpp"
#include "backend/file/FileInputChannel.hpp"
#include "backend/file/FileOutputChannel.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace Proxy::Backend::File {

/**
 * @brief File-based transport backend implementing Backend::IBackend.
 *
 * Configures file transport settings and instantiates FileInputChannel and FileOutputChannel.
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
     * @brief Creates an input channel watching a directory configured for this channel.
     *
     * Checks "channel.<name>.path", falling back to "file.path" or "/tmp/watch/<name>".
     */
    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view name, const ::Backend::IConfig& config) override;

    /**
     * @brief Creates an output channel writing files into a directory configured for this channel.
     *
     * Checks "channel.<name>.path", falling back to "file.path" or "/tmp/watch/<name>".
     */
    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view name, const ::Backend::IConfig& config) override;

private:
    std::filesystem::path m_watchDirectory { "/tmp/watch" };
};

} // namespace Proxy::Backend::File
