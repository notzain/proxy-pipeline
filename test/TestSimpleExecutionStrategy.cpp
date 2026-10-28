#include <catch2/catch_test_macros.hpp>
#include "backend/IBackend.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "backend/MessageEnvelope.hpp"
#include "backend/MessageResult.hpp"
#include "backend/MessageView.hpp"
#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class SimpleTestHandler : public Core::Handler::IHandler {
public:
    explicit SimpleTestHandler(Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    Backend::MessageResult onMessageReceived(
        std::string_view /*channelName*/,
        const Backend::MessageView& message) override
    {
        m_receivedPayloads.push_back(std::string(
            reinterpret_cast<const char*>(message.payload().data()),
            message.payload().size()));
        return Backend::Consumed {};
    }

    void step() override
    {
        m_steps++;
    }

    std::vector<std::string> m_receivedPayloads;
    std::atomic<std::size_t> m_steps { 0 };
};

} // namespace

TEST_CASE("SimpleExecutionStrategy runs deterministic message loop", "[threading][simple]")
{
    Core::Simulation::SimulationBackend backend;
    SimpleTestHandler handler(backend);

    auto channel = std::make_unique<Core::Simulation::MockInputChannel>("in");
    auto* rawChannel = channel.get();
    handler.registerInputChannel(std::move(channel));

    SECTION("Processes standard Proxy envelopes deterministically")
    {
        const std::string testMsg = "Hello Simple Strategy";
        auto env = Backend::MessageEnvelope::packRaw(
            Common::Compat::as_bytes(Common::Compat::span<const char>(testMsg.data(), testMsg.size())));
        rawChannel->pushMessage(Common::Compat::span<const std::byte>(env.data(), env.size()));

        Core::Handler::HandlerRunner runner(handler);
        Core::Config::Configuration config;
        config.set("execution.model", "simple");
        REQUIRE(runner.configure(config));

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (handler.m_receivedPayloads.empty() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.m_receivedPayloads.size() == 1);
        REQUIRE(handler.m_receivedPayloads.front() == testMsg);
        REQUIRE(handler.m_steps.load() > 0);
    }
}

