#include "log/ConsoleLogSink.hpp"
#include "log/FileLogSink.hpp"
#include "log/Log.hpp"
#include "log/SocketLogSink.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

class MemoryLogSink : public Proxy::Log::ILogSink {
public:
    void log(Proxy::Log::Level level, std::string_view message) override
    {
        if (static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(m_level)) {
            messages.emplace_back(message);
        }
    }

    void flush() override
    {
        flushed = true;
    }

    void setLevel(Proxy::Log::Level level) override
    {
        m_level = level;
    }

    [[nodiscard]] Proxy::Log::Level getLevel() const override
    {
        return m_level;
    }

    std::vector<std::string> messages;
    bool flushed { false };

private:
    Proxy::Log::Level m_level { Proxy::Log::Level::Info };
};

} // namespace

TEST_CASE("Proxy::Log level management", "[log]")
{
    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());

    Proxy::Log::setLevel(Proxy::Log::Level::Info);
    CHECK(Proxy::Log::getLevel() == Proxy::Log::Level::Info);

    Proxy::Log::setLevel(Proxy::Log::Level::Debug);
    CHECK(Proxy::Log::getLevel() == Proxy::Log::Level::Debug);

    Proxy::Log::setLevel(Proxy::Log::Level::Warn);
    CHECK(Proxy::Log::getLevel() == Proxy::Log::Level::Warn);

    Proxy::Log::setLevel(Proxy::Log::Level::Info);
}

TEST_CASE("Proxy::Log file logging initialization and writes via FileLogSink", "[log]")
{
    const std::filesystem::path testLogDir = std::filesystem::path("/tmp") / "proxylog_test";
    const std::filesystem::path testLogFile = testLogDir / "test_run.log";

    std::error_code ec;
    std::filesystem::remove_all(testLogDir, ec);

    Proxy::Log::init(std::make_unique<Proxy::Log::FileLogSink>(testLogFile, false));
    Proxy::Log::setLevel(Proxy::Log::Level::Debug);

    std::string uniqueMarker = "Marker_Test_FileLogging_12345";
    Proxy::Log::info("Message with marker: {}", uniqueMarker);
    Proxy::Log::flush();

    REQUIRE(std::filesystem::exists(testLogFile));

    std::ifstream stream(testLogFile);
    REQUIRE(stream.is_open());
    std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();

    CHECK(content.find(uniqueMarker) != std::string::npos);

    std::filesystem::remove_all(testLogDir, ec);
    Proxy::Log::init(nullptr);
}

TEST_CASE("Proxy::Log socket logging initialization via SocketLogSink", "[log]")
{
    CHECK_NOTHROW(Proxy::Log::init(std::make_unique<Proxy::Log::SocketLogSink>("127.0.0.1", 54321, false)));
    CHECK_NOTHROW(Proxy::Log::info("UDP socket message from test"));
    CHECK_NOTHROW(Proxy::Log::flush());

    Proxy::Log::init(nullptr);
}

TEST_CASE("Proxy::Log custom ILogSink implementation", "[log]")
{
    auto memorySink = std::make_unique<MemoryLogSink>();
    auto* rawSink = memorySink.get();

    Proxy::Log::init(std::move(memorySink));
    Proxy::Log::setLevel(Proxy::Log::Level::Debug);

    Proxy::Log::debug("Debug message");
    Proxy::Log::info("Hello {}", "World");
    Proxy::Log::warn("Warning: count is {}", 42);
    Proxy::Log::flush();

    REQUIRE(rawSink->messages.size() == 3);
    CHECK(rawSink->messages[0] == "Debug message");
    CHECK(rawSink->messages[1] == "Hello World");
    CHECK(rawSink->messages[2] == "Warning: count is 42");
    CHECK(rawSink->flushed);

    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
}
