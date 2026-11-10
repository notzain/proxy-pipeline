#include "log/ConsoleLogSink.hpp"
#include "log/FileLogSink.hpp"
#include "log/JsonLogSink.hpp"
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

TEST_CASE("Proxy::Log compile-time extractStem", "[log]")
{
    using Proxy::Log::Detail::extractStem;
    STATIC_CHECK(extractStem("/path/to/GatherHandler.cpp") == "GatherHandler");
    STATIC_CHECK(extractStem("C:\\source\\dummy_receive\\DummyReceiveHandler.cpp") == "DummyReceiveHandler");
    STATIC_CHECK(extractStem("MyFile.hpp") == "MyFile");
    STATIC_CHECK(extractStem("Plain") == "Plain");
    STATIC_CHECK(extractStem("") == "");
}

class StructuredCaptureSink : public Proxy::Log::ILogSink {
public:
    void log(Proxy::Log::Level level, std::string_view message) override
    {
        lastMessage = std::string(message);
        lastLevel = level;
    }

    void logStructured(const Proxy::Log::StructuredLogRecord& record) override
    {
        lastRecord = record;
        lastMessage = std::string(record.message);
        lastComponent = std::string(record.component);
        lastFields.clear();
        for (size_t i = 0; i < record.fieldCount; ++i) {
            lastFields.push_back(record.fields[i]);
        }
    }

    void flush() override { }
    void setLevel(Proxy::Log::Level level) override { m_level = level; }
    [[nodiscard]] Proxy::Log::Level getLevel() const override { return m_level; }

    std::string lastMessage;
    std::string lastComponent;
    Proxy::Log::Level lastLevel { Proxy::Log::Level::Info };
    Proxy::Log::StructuredLogRecord lastRecord;
    std::vector<Proxy::Log::LogField> lastFields;

private:
    Proxy::Log::Level m_level { Proxy::Log::Level::Trace };
};

TEST_CASE("Proxy::Log PROXY_LOG macros with named placeholders and dual-mode auto-extractor", "[log]")
{
    auto captureSink = std::make_unique<StructuredCaptureSink>();
    auto* sink = captureSink.get();
    Proxy::Log::init(std::move(captureSink));
    Proxy::Log::setLevel(Proxy::Log::Level::Trace);

    SECTION("Simple message without arguments")
    {
        PROXY_LOG_INFO("Processing started");
        CHECK(sink->lastComponent == "TestLog");
        CHECK(sink->lastMessage == "Processing started");
        CHECK(sink->lastFields.empty());
    }

    SECTION("Named placeholder extraction in natural sentence")
    {
        int bytes = 512;
        std::string_view ch = "udp_in";
        PROXY_LOG_INFO("Channel {channel} received {bytes} bytes", ch, bytes);

        CHECK(sink->lastComponent == "TestLog");
        CHECK(sink->lastMessage == "Channel udp_in received 512 bytes");
        REQUIRE(sink->lastFields.size() == 2);
        CHECK(sink->lastFields[0].key() == "channel");
        CHECK(std::get<std::string_view>(sink->lastFields[0].value()) == "udp_in");
        CHECK(sink->lastFields[1].key() == "bytes");
        CHECK(std::get<int64_t>(sink->lastFields[1].value()) == 512);
    }

    SECTION("Dual-Mode Auto-Extractor: trailing variables without placeholders")
    {
        uint64_t droppedCount = 3;
        bool isThrottled = true;
        PROXY_LOG_WARN("Buffer overflow occurred", droppedCount, isThrottled);

        CHECK(sink->lastComponent == "TestLog");
        CHECK(sink->lastMessage == "Buffer overflow occurred");
        CHECK(sink->lastRecord.isInterpolated == false);
        REQUIRE(sink->lastFields.size() == 2);
        CHECK(sink->lastFields[0].key() == "droppedCount");
        CHECK(std::get<uint64_t>(sink->lastFields[0].value()) == 3);
        CHECK(sink->lastFields[1].key() == "isThrottled");
        CHECK(std::get<bool>(sink->lastFields[1].value()) == true);
    }

    SECTION("Anonymous {} placeholder interpolation and structured field extraction")
    {
        std::string_view channel = "udp_data_link";
        int messageSize = 128;
        PROXY_LOG_INFO("Sending message to channel '{}': {} bytes", channel, messageSize);

        CHECK(sink->lastComponent == "TestLog");
        CHECK(sink->lastMessage == "Sending message to channel 'udp_data_link': 128 bytes");
        CHECK(sink->lastRecord.isInterpolated == true);
        REQUIRE(sink->lastFields.size() == 2);
        CHECK(sink->lastFields[0].key() == "channel");
        CHECK(std::get<std::string_view>(sink->lastFields[0].value()) == "udp_data_link");
        CHECK(sink->lastFields[1].key() == "messageSize");
        CHECK(std::get<int64_t>(sink->lastFields[1].value()) == 128);
    }

    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
}

TEST_CASE("Proxy::Log JsonLogSink serialization to file", "[log]")
{
    const std::filesystem::path testDir = std::filesystem::path("/tmp") / "proxylog_json_test";
    const std::filesystem::path testFile = testDir / "structured.log";

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    Proxy::Log::init(std::make_unique<Proxy::Log::JsonLogSink>(testFile.string(), false));
    Proxy::Log::setLevel(Proxy::Log::Level::Debug);

    int packetId = 42;
    std::string_view target = "node_alpha";
    PROXY_LOG_INFO("Dispatched packet {packet_id} to {target}", packetId, target);
    Proxy::Log::flush();

    REQUIRE(std::filesystem::exists(testFile));
    std::ifstream stream(testFile);
    REQUIRE(stream.is_open());
    std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();

    CHECK(content.find("\"component\":\"TestLog\"") != std::string::npos);
    CHECK(content.find("\"packet_id\":42") != std::string::npos);
    CHECK(content.find("\"target\":\"node_alpha\"") != std::string::npos);
    CHECK(content.find("\"level\":\"INFO\"") != std::string::npos);

    std::filesystem::remove_all(testDir, ec);
    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
}
