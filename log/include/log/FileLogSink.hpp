#pragma once

#include "log/ILogSink.hpp"
#include <filesystem>
#include <memory>

namespace Proxy::Log {

/**
 * @brief Log sink that writes log entries to a designated file.
 */
class FileLogSink : public ILogSink {
public:
    /**
     * @brief Constructs a new FileLogSink.
     *
     * @param logPath Target file path for log messages.
     * @param alsoLogToConsole Whether to mirror log entries to console.
     */
    explicit FileLogSink(const std::filesystem::path& logPath, bool alsoLogToConsole = true);

    /**
     * @brief Destructor.
     */
    ~FileLogSink() override;

    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;
    FileLogSink(FileLogSink&&) noexcept;
    FileLogSink& operator=(FileLogSink&&) noexcept;

    void log(Level level, std::string_view message) override;
    void flush() override;
    void setLevel(Level level) override;
    [[nodiscard]] Level getLevel() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Log
