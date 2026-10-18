#pragma once

#include "log/ILogSink.hpp"
#include <memory>

namespace Proxy::Log {

/**
 * @brief Log sink that outputs formatted log messages to standard output / error.
 */
class ConsoleLogSink : public ILogSink {
public:
    /**
     * @brief Constructs a new ConsoleLogSink.
     */
    ConsoleLogSink();

    /**
     * @brief Destructor.
     */
    ~ConsoleLogSink() override;

    ConsoleLogSink(const ConsoleLogSink&) = delete;
    ConsoleLogSink& operator=(const ConsoleLogSink&) = delete;
    ConsoleLogSink(ConsoleLogSink&&) noexcept;
    ConsoleLogSink& operator=(ConsoleLogSink&&) noexcept;

    void log(Level level, std::string_view message) override;
    void flush() override;
    void setLevel(Level level) override;
    [[nodiscard]] Level getLevel() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Log
