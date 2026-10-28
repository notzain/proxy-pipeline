#include "backend/ISocketPoller.hpp"
#include "common/compat/Latch.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "core/handler/ExecutionModel.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fmt/core.h>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

class MockInputChannel : public Backend::IInputChannel {
public:
    MockInputChannel(std::string name, std::vector<std::string> messages, bool appendMalformed = false)
        : Backend::IInputChannel(std::move(name))
    {
        for (const auto& msg : messages) {
            auto env = Core::Message::MessageEnvelope::packRaw(Common::Compat::as_bytes(Common::Compat::span<const char>(msg.data(), msg.size())));
            m_messages.emplace_back(reinterpret_cast<const char*>(env.data()), env.size());
        }
        if (appendMalformed) {
            m_messages.emplace_back("invalid envelope");
        }
    }

    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout) override
    {
        std::lock_guard lock(m_mutex);
        if (m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            const auto bytesToCopy = std::min(destination.size(), msg.size());
            std::memcpy(destination.data(), msg.data(), bytesToCopy);
            return BytesResult::success(bytesToCopy);
        }
        return BytesResult::failure(Backend::receiveTimeout());
    }

    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout) override
    {
        std::lock_guard lock(m_mutex);
        if (slots.empty()) {
            return BatchResult::failure({ 0, Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
        }
        std::size_t count = 0;
        while (count < slots.size() && m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            const auto bytesToCopy = std::min(slots[count].buffer.size(), msg.size());
            std::memcpy(slots[count].buffer.data(), msg.data(), bytesToCopy);
            slots[count].size = bytesToCopy;
            ++count;
        }
        return count > 0
            ? BatchResult::success(count)
            : BatchResult::failure({ 0, Backend::receiveTimeout() });
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_messages;
    std::size_t m_index { 0 };
};

class StatusInputChannel : public Backend::IInputChannel {
public:
    explicit StatusInputChannel(Backend::ReceiveError error)
        : m_error(std::move(error))
    {
    }

    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout) override
    {
        return BytesResult::failure(m_error);
    }

private:
    Backend::ReceiveError m_error;
};

class PartialFailureInputChannel : public Backend::IInputChannel {
public:
    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout) override
    {
        if (m_received) {
            return BytesResult::failure(Backend::receiveFailure(std::make_error_code(std::errc::io_error)));
        }
        destination[0] = std::byte { 0x2a };
        m_received = true;
        return BytesResult::success(1);
    }

private:
    bool m_received { false };
};

class StepWaitingInputChannel : public MockInputChannel {
public:
    StepWaitingInputChannel(std::string name, std::vector<std::string> messages, Common::Compat::latch& stepStarted)
        : MockInputChannel(std::move(name), std::move(messages))
        , m_stepStarted(stepStarted)
    {
    }

    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override
    {
        if (!m_waited) {
            m_waited = true;
            m_stepStarted.wait();
        }
        return MockInputChannel::receiveBatch(slots, timeout);
    }

private:
    Common::Compat::latch& m_stepStarted;
    bool m_waited { false };
};

TEST_CASE("Input channel receive results distinguish data, timeout, closure, and errors", "[channel][receive]")
{
    std::array<std::byte, 8> buffer { };

    auto successfulResult = Backend::Result<int, std::string>::success(42);
    REQUIRE(successfulResult.hasValue());
    CHECK(successfulResult.value() == 42);
    auto failedResult = Backend::Result<int, std::string>::failure("failure");
    REQUIRE_FALSE(failedResult.hasValue());
    CHECK(failedResult.error() == "failure");

    StatusInputChannel timeoutChannel(Backend::receiveTimeout());
    auto timeout = timeoutChannel.receiveInto(buffer, std::chrono::milliseconds(0));
    REQUIRE_FALSE(timeout.hasValue());
    CHECK(Backend::isReceiveTimeout(timeout.error()));

    StatusInputChannel closedChannel(Backend::receiveClosed());
    auto closed = closedChannel.receiveInto(buffer, std::chrono::milliseconds(0));
    REQUIRE_FALSE(closed.hasValue());
    CHECK(Backend::isReceiveClosed(closed.error()));

    StatusInputChannel errorChannel(Backend::receiveFailure(std::make_error_code(std::errc::permission_denied)));
    auto error = errorChannel.receiveInto(buffer, std::chrono::milliseconds(0));
    REQUIRE_FALSE(error.hasValue());
    const auto* receiveFailure = std::get_if<Backend::ReceiveFailure>(&error.error());
    REQUIRE(receiveFailure != nullptr);
    CHECK(receiveFailure->code == std::make_error_code(std::errc::permission_denied));

    auto poller = Backend::createSocketPoller();
    auto closedPoller = poller->receive(-1, buffer, std::chrono::milliseconds(0));
    REQUIRE_FALSE(closedPoller.hasValue());
    CHECK(Backend::isReceiveClosed(closedPoller.error()));

    ::Backend::BufferSlot slot { buffer, 0 };
    auto batchTimeout = timeoutChannel.receiveBatch(Common::Compat::span<::Backend::BufferSlot>(&slot, 1), std::chrono::milliseconds(0));
    REQUIRE_FALSE(batchTimeout.hasValue());
    CHECK(Backend::isReceiveTimeout(batchTimeout.error().cause));
    auto batchError = errorChannel.receiveBatch(Common::Compat::span<::Backend::BufferSlot>(&slot, 1), std::chrono::milliseconds(0));
    REQUIRE_FALSE(batchError.hasValue());
    const auto* batchReceiveFailure = std::get_if<Backend::ReceiveFailure>(&batchError.error().cause);
    REQUIRE(batchReceiveFailure != nullptr);
    CHECK(batchReceiveFailure->code == std::make_error_code(std::errc::permission_denied));

    PartialFailureInputChannel partialFailure;
    std::array<::Backend::BufferSlot, 2> slots {
        ::Backend::BufferSlot { buffer, 0 },
        ::Backend::BufferSlot { buffer, 0 },
    };
    auto partialResult = partialFailure.receiveBatch(slots, std::chrono::milliseconds(0));
    REQUIRE_FALSE(partialResult.hasValue());
    CHECK(partialResult.error().receivedCount == 1);
    const auto* partialReceiveFailure = std::get_if<Backend::ReceiveFailure>(&partialResult.error().cause);
    REQUIRE(partialReceiveFailure != nullptr);
    CHECK(partialReceiveFailure->code == std::make_error_code(std::errc::io_error));
    CHECK(slots[0].size == 1);
}

class MockOutputChannel : public Backend::IOutputChannel {
public:
    bool send(Common::Compat::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        auto optView = Core::Message::MessageView::from(message);
        std::string str = optView
            ? std::string(reinterpret_cast<const char*>(optView->payload().data()), optView->payload().size())
            : std::string(reinterpret_cast<const char*>(message.data()), message.size());
        sentMessages.push_back(str);
        return true;
    }

    std::mutex m_mutex;
    std::vector<std::string> sentMessages;
};

class TestChainedHandler : public Core::Handler::IHandler {
public:
    explicit TestChainedHandler(::Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    void setRunner(Core::Handler::HandlerRunner* runner)
    {
        m_runner = runner;
    }

    void setOutChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> out)
    {
        m_outChannel = out;
    }

    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override
    {
        std::lock_guard lock(m_handlerMutex);
        std::string text(reinterpret_cast<const char*>(message.payload().data()), message.payload().size());
        receivedMessages.push_back(fmt::format("{}:{}", channelName, text));

        if (m_outChannel) {
            m_outChannel->send(message.rawBuffer());
        }

        m_count++;
        if (m_count >= 2 && m_runner) {
            m_runner->stop();
        }
        return Core::Message::Forwarded { };
    }

    std::mutex m_handlerMutex;
    std::vector<std::string> receivedMessages;

private:
    Core::Handler::HandlerRunner* m_runner { nullptr };
    Common::Memory::ObserverPtr<::Backend::IOutputChannel> m_outChannel;
    int m_count { 0 };
};

class TelemetryAccountingHandler : public Core::Handler::IHandler {
public:
    explicit TelemetryAccountingHandler(::Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    void setRunner(Core::Handler::HandlerRunner* runner)
    {
        m_runner = runner;
    }

    Core::Message::MessageResult onMessageReceived(
        std::string_view,
        const Core::Message::MessageView&) override
    {
        receivedCount.fetch_add(1, std::memory_order_relaxed);
        m_runner->stop();
        return Core::Message::Consumed { };
    }

    std::atomic<std::size_t> receivedCount { 0 };

private:
    Core::Handler::HandlerRunner* m_runner { nullptr };
};

class SerializedAccessHandler : public Core::Handler::IHandler {
public:
    explicit SerializedAccessHandler(::Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    void setRunner(Core::Handler::HandlerRunner* runner)
    {
        m_runner = runner;
    }

    void setStepStarted(Common::Compat::latch* stepStarted)
    {
        m_stepStarted = stepStarted;
    }

    Core::Message::MessageResult onMessageReceived(
        std::string_view,
        const Core::Message::MessageView&) override
    {
        recordWork(std::chrono::milliseconds(2));
        if (m_received.fetch_add(1, std::memory_order_relaxed) + 1 == 2 && m_runner) {
            m_runner->stop();
        }
        return Core::Message::Consumed { };
    }

    void step() override
    {
        if (m_stepStarted && m_firstStep.exchange(false, std::memory_order_relaxed)) {
            recordWork(std::chrono::milliseconds(20), m_stepStarted);
            return;
        }
        recordWork(std::chrono::milliseconds(1));
    }

    [[nodiscard]] std::size_t maxConcurrentCalls() const noexcept
    {
        return m_maxConcurrentCalls.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t receivedCount() const noexcept
    {
        return m_received.load(std::memory_order_relaxed);
    }

private:
    void recordWork(std::chrono::milliseconds duration, Common::Compat::latch* stepStarted = nullptr)
    {
        const auto active = m_activeCalls.fetch_add(1, std::memory_order_acq_rel) + 1;
        auto maxActive = m_maxConcurrentCalls.load(std::memory_order_relaxed);
        while (active > maxActive
            && !m_maxConcurrentCalls.compare_exchange_weak(
                maxActive, active, std::memory_order_relaxed)) {
        }
        if (stepStarted) {
            stepStarted->count_down();
        }
        std::this_thread::sleep_for(duration);
        m_activeCalls.fetch_sub(1, std::memory_order_release);
    }

    Core::Handler::HandlerRunner* m_runner { nullptr };
    Common::Compat::latch* m_stepStarted { nullptr };
    std::atomic<bool> m_firstStep { true };
    std::atomic<std::size_t> m_activeCalls { 0 };
    std::atomic<std::size_t> m_maxConcurrentCalls { 0 };
    std::atomic<std::size_t> m_received { 0 };
};

class ParallelCallbacksHandler : public Core::Handler::IHandler {
public:
    explicit ParallelCallbacksHandler(::Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    void setRunner(Core::Handler::HandlerRunner* runner)
    {
        m_runner = runner;
    }

    Core::Message::MessageResult onMessageReceived(
        std::string_view,
        const Core::Message::MessageView&) override
    {
        const auto active = m_activeCalls.fetch_add(1, std::memory_order_acq_rel) + 1;
        auto maxActive = m_maxConcurrentCalls.load(std::memory_order_relaxed);
        while (active > maxActive
            && !m_maxConcurrentCalls.compare_exchange_weak(
                maxActive, active, std::memory_order_relaxed)) {
        }

        {
            std::unique_lock lock(m_mutex);
            ++m_arrived;
            m_condition.notify_all();
            m_condition.wait_for(lock, std::chrono::seconds(1), [this] {
                return m_arrived == 2;
            });
        }

        m_activeCalls.fetch_sub(1, std::memory_order_release);
        if (m_received.fetch_add(1, std::memory_order_relaxed) + 1 == 2 && m_runner) {
            m_runner->stop();
        }
        return Core::Message::Consumed { };
    }

    [[nodiscard]] std::size_t maxConcurrentCalls() const noexcept
    {
        return m_maxConcurrentCalls.load(std::memory_order_relaxed);
    }

private:
    Core::Handler::HandlerRunner* m_runner { nullptr };
    std::atomic<std::size_t> m_activeCalls { 0 };
    std::atomic<std::size_t> m_maxConcurrentCalls { 0 };
    std::atomic<std::size_t> m_received { 0 };
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::size_t m_arrived { 0 };
};

} // namespace

TEST_CASE("IHandler visits input channels without exposing ownership storage", "[channel][core]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);

    CHECK(handler.inputChannelCount() == 0);

    auto first = std::make_unique<MockInputChannel>("registered-first", std::vector<std::string> { });
    auto second = std::make_unique<MockInputChannel>("registered-second", std::vector<std::string> { });
    auto* firstPtr = first.get();
    auto* secondPtr = second.get();
    handler.registerInputChannel(std::move(first));
    handler.registerInputChannel(std::move(second));

    CHECK(handler.inputChannelCount() == 2);

    std::vector<std::string> names;
    std::vector<::Backend::IInputChannel*> channels;
    handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
        names.emplace_back(channel.name());
        channels.push_back(&channel);
    });

    CHECK(names == std::vector<std::string> { "registered-first", "registered-second" });
    REQUIRE(channels.size() == 2);
    CHECK(channels[0] == firstPtr);
    CHECK(channels[1] == secondPtr);
}

TEST_CASE("IHandler output channel registration and direct dispatch", "[channel][core]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);
    auto mockChan = std::make_unique<MockOutputChannel>();
    auto* rawChan = mockChan.get();

    auto registered = handler.registerOutputChannel(std::move(mockChan));
    CHECK(registered == rawChan);
    CHECK_THROWS_AS(handler.registerOutputChannel(nullptr), std::invalid_argument);

    std::string msg = "Hello Sender";
    CHECK(registered->send(Common::Memory::ByteView(msg)));

    REQUIRE(rawChan->sentMessages.size() == 1);
    CHECK(rawChan->sentMessages[0] == "Hello Sender");
}

TEST_CASE("IHandler Sequential execution model", "[channel][core]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::SequentialModel { });
    handler.setRunner(&runner);

    auto inChan1 = std::make_unique<MockInputChannel>("chan1", std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>("chan2", std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel(std::move(inChan1));
    handler.registerInputChannel(std::move(inChan2));
    handler.setOutChannel(handler.registerOutputChannel(std::move(outChan)));

    runner.start();

    REQUIRE(handler.receivedMessages.size() == 2);
    CHECK(handler.receivedMessages[0] == "chan1:msgA");
    CHECK(handler.receivedMessages[1] == "chan2:msgB");

    REQUIRE(rawOutChan->sentMessages.size() == 2);
    CHECK(rawOutChan->sentMessages[0] == "msgA");
    CHECK(rawOutChan->sentMessages[1] == "msgB");
}

TEST_CASE("IHandler Dedicated thread execution model with async start", "[channel][core]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::DedicatedThreadModel { });
    handler.setRunner(&runner);

    auto inChan1 = std::make_unique<MockInputChannel>("chan1", std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>("chan2", std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel(std::move(inChan1));
    handler.registerInputChannel(std::move(inChan2));
    handler.setOutChannel(handler.registerOutputChannel(std::move(outChan)));

    runner.startAsync();
    runner.wait();

    REQUIRE(handler.receivedMessages.size() == 2);
    REQUIRE(rawOutChan->sentMessages.size() == 2);
}

TEST_CASE("IHandler ThreadPool execution model", "[channel][core]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::ThreadPoolModel { .poolSize = 2 });
    handler.setRunner(&runner);

    auto inChan1 = std::make_unique<MockInputChannel>("chan1", std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>("chan2", std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel(std::move(inChan1));
    handler.registerInputChannel(std::move(inChan2));
    handler.setOutChannel(handler.registerOutputChannel(std::move(outChan)));

    runner.startAsync();
    runner.wait();

    REQUIRE(handler.receivedMessages.size() == 2);
    REQUIRE(rawOutChan->sentMessages.size() == 2);
}

TEST_CASE("Ingress accounting is consistent across execution models", "[channel][core][telemetry]")
{
    auto& registry = Core::Telemetry::MetricsRegistry::instance();
    auto& ingressCounter = registry.counter("messages_ingress_total");
    auto& bytesCounter = registry.counter("bytes_ingress_total");
    auto& droppedCounter = registry.counter("messages_dropped_total");
    auto& latencyHistogram = registry.latencyHistogram("message_processing_duration_us");

    const auto initialIngress = ingressCounter.value();
    const auto initialBytes = bytesCounter.value();
    const auto initialDropped = droppedCounter.value();
    const auto initialLatency = latencyHistogram.count();

    const std::string payload = "valid";
    const auto expectedBytes = Core::Message::MessageEnvelope::packRaw(
        Common::Compat::as_bytes(Common::Compat::span<const char>(payload.data(), payload.size())))
                                   .size();

    const std::array<Core::Handler::ExecutionModel, 3> models {
        Core::Handler::SequentialModel { },
        Core::Handler::DedicatedThreadModel { },
        Core::Handler::ThreadPoolModel { .poolSize = 2 }
    };

    for (const auto& model : models) {
        Core::Simulation::SimulationBackend backend;
        TelemetryAccountingHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(model);
        handler.setRunner(&runner);
        handler.registerInputChannel(std::make_unique<MockInputChannel>(
            "accounting", std::vector<std::string> { payload }, true));

        runner.startAsync();
        runner.wait();

        REQUIRE(handler.receivedCount.load(std::memory_order_relaxed) == 1);
    }

    CHECK(ingressCounter.value() - initialIngress == models.size());
    CHECK(bytesCounter.value() - initialBytes == expectedBytes * models.size());
    CHECK(droppedCounter.value() - initialDropped == models.size());
    CHECK(latencyHistogram.count() - initialLatency == models.size());
}

TEST_CASE("Parallel execution models can serialize handler callbacks and step", "[channel][core]")
{
    const std::array<Core::Handler::ExecutionModel, 2> models {
        Core::Handler::DedicatedThreadModel { .serializeHandlerAccess = true },
        Core::Handler::ThreadPoolModel {
            .poolSize = 2,
            .strictFifo = true,
            .serializeHandlerAccess = true }
    };

    for (const auto& model : models) {
        Core::Simulation::SimulationBackend backend;
        SerializedAccessHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(model);
        handler.setRunner(&runner);
        Common::Compat::latch stepStarted(1);
        handler.setStepStarted(&stepStarted);
        handler.registerInputChannel(std::make_unique<StepWaitingInputChannel>(
            "first", std::vector<std::string> { "one" }, stepStarted));
        handler.registerInputChannel(std::make_unique<StepWaitingInputChannel>(
            "second", std::vector<std::string> { "two" }, stepStarted));

        runner.startAsync();
        runner.wait();

        CHECK(handler.receivedCount() == 2);
        CHECK(handler.maxConcurrentCalls() == 1);
    }
}

TEST_CASE("Parallel execution models remain concurrent by default", "[channel][core]")
{
    const std::array<Core::Handler::ExecutionModel, 2> models {
        Core::Handler::DedicatedThreadModel { },
        Core::Handler::ThreadPoolModel { .poolSize = 2 }
    };

    for (const auto& model : models) {
        Core::Simulation::SimulationBackend backend;
        ParallelCallbacksHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(model);
        handler.setRunner(&runner);
        handler.registerInputChannel(std::make_unique<MockInputChannel>(
            "first", std::vector<std::string> { "one" }));
        handler.registerInputChannel(std::make_unique<MockInputChannel>(
            "second", std::vector<std::string> { "two" }));

        runner.startAsync();
        runner.wait();

        CHECK(handler.maxConcurrentCalls() == 2);
    }
}

TEST_CASE("Execution model configuration enables serialized handler access", "[channel][core]")
{
    Core::Config::Configuration config;
    config.set("execution.model", "thread_pool");
    config.set("execution.serialize_handler_access", "true");

    const auto model = Core::Handler::parseExecutionModel(config);
    REQUIRE(std::holds_alternative<Core::Handler::ThreadPoolModel>(model));
    CHECK(std::get<Core::Handler::ThreadPoolModel>(model).serializeHandlerAccess);
}

TEST_CASE("HandlerRunner stops safely after SIGTERM", "[channel][core][signal]")
{
    struct sigaction previousAction { };
    REQUIRE(sigaction(SIGTERM, nullptr, &previousAction) == 0);

    Core::Simulation::SimulationBackend backend;
    TestChainedHandler handler(backend);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::DedicatedThreadModel { });

    runner.startAsync();
    REQUIRE(runner.isRunning());
    REQUIRE(std::raise(SIGTERM) == 0);
    runner.wait();

    struct sigaction restoredAction { };
    REQUIRE(sigaction(SIGTERM, nullptr, &restoredAction) == 0);
    CHECK(restoredAction.sa_handler == previousAction.sa_handler);
    CHECK_FALSE(runner.isRunning());
}

TEST_CASE("Only one HandlerRunner owns process signal handling", "[channel][core][signal]")
{
    Core::Simulation::SimulationBackend backend;
    TestChainedHandler activeHandler(backend);
    TestChainedHandler secondHandler(backend);
    Core::Handler::HandlerRunner activeRunner(activeHandler);
    Core::Handler::HandlerRunner secondRunner(secondHandler);
    activeRunner.setExecutionModel(Core::Handler::DedicatedThreadModel { });

    activeRunner.startAsync();
    REQUIRE(activeRunner.isRunning());
    CHECK_THROWS_AS(secondRunner.startAsync(), std::logic_error);
    REQUIRE(std::raise(SIGINT) == 0);
    activeRunner.wait();

    CHECK_FALSE(activeRunner.isRunning());
}

TEST_CASE("Direct output channel multi-dispatch", "[channel][core]")
{
    auto chanA = std::make_unique<MockOutputChannel>();
    auto chanB = std::make_unique<MockOutputChannel>();
    auto* rawA = chanA.get();
    auto* rawB = chanB.get();

    std::vector<::Backend::IOutputChannel*> channels = { rawA, rawB };

    std::string msg = "Broadcast Message";
    Common::Memory::ByteView byteSpan(msg);

    std::size_t sentCount = 0;
    for (auto* ch : channels) {
        if (ch->send(byteSpan)) {
            sentCount++;
        }
    }

    CHECK(sentCount == 2);
    REQUIRE(rawA->sentMessages.size() == 1);
    CHECK(rawA->sentMessages[0] == "Broadcast Message");
    REQUIRE(rawB->sentMessages.size() == 1);
    CHECK(rawB->sentMessages[0] == "Broadcast Message");
}

TEST_CASE("MergeHandler redirects multiple input channels to single output channel", "[channel][core]")
{
    class MockMergeHandler : public Core::Handler::IHandler {
    public:
        explicit MockMergeHandler(::Backend::IBackend& backend)
            : Core::Handler::IHandler(backend)
        {
        }

        void setRunner(Core::Handler::HandlerRunner* runner)
        {
            m_runner = runner;
        }

        void setOutChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> out)
        {
            m_outChannel = out;
        }

        Core::Message::MessageResult onMessageReceived(std::string_view, const Core::Message::MessageView& message) override
        {
            if (m_outChannel) {
                m_outChannel->send(message.rawBuffer());
            }
            m_count++;
            if (m_count >= 2 && m_runner) {
                m_runner->stop();
            }
            return Core::Message::Forwarded { };
        }

    private:
        Core::Handler::HandlerRunner* m_runner { nullptr };
        Common::Memory::ObserverPtr<::Backend::IOutputChannel> m_outChannel;
        std::atomic<int> m_count { 0 };
    };

    Core::Simulation::SimulationBackend backend;
    MockMergeHandler merge(backend);
    Core::Handler::HandlerRunner runner(merge);
    runner.setExecutionModel(Core::Handler::SequentialModel { });
    merge.setRunner(&runner);

    auto inA = std::make_unique<MockInputChannel>("in_a", std::vector<std::string> { "input1" });
    auto inB = std::make_unique<MockInputChannel>("in_b", std::vector<std::string> { "input2" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOut = outChan.get();

    merge.registerInputChannel(std::move(inA));
    merge.registerInputChannel(std::move(inB));
    merge.setOutChannel(merge.registerOutputChannel(std::move(outChan)));

    runner.start();

    REQUIRE(rawOut->sentMessages.size() == 2);
}

#include "backend/uds/UdsInputChannel.hpp"
#include "backend/uds/UdsOutputChannel.hpp"

TEST_CASE("UDS Input and Output channels send and receive messages", "[channel][uds]")
{
    const std::string sockPath = "/tmp/test_proxy_channel.sock";
    Proxy::Backend::Uds::UdsInputChannel inChannel("uds_in", sockPath);
    Proxy::Backend::Uds::UdsOutputChannel outChannel("uds_out", sockPath);

    std::string testMsg = "Hello UDS IPC!";
    Common::Memory::ByteView sendSpan(testMsg);

    std::array<std::byte, 128> recvBuf { };
    auto timeoutResult = inChannel.receiveInto(recvBuf, std::chrono::milliseconds(0));
    REQUIRE_FALSE(timeoutResult.hasValue());
    CHECK(Backend::isReceiveTimeout(timeoutResult.error()));

    REQUIRE(outChannel.send(sendSpan));

    auto result = inChannel.receiveInto(recvBuf, std::chrono::milliseconds(500));
    REQUIRE(result.hasValue());
    REQUIRE(result.value() == testMsg.size());

    std::string receivedStr(reinterpret_cast<const char*>(recvBuf.data()), result.value());
    CHECK(receivedStr == testMsg);
}

TEST_CASE("UDS Input channel drains multiple packets via receiveBatch", "[channel][uds][batch]")
{
    const std::string sockPath = "/tmp/test_proxy_uds_batch.sock";
    Proxy::Backend::Uds::UdsInputChannel inChannel("uds_in", sockPath);
    Proxy::Backend::Uds::UdsOutputChannel outChannel("uds_out", sockPath);

    constexpr std::size_t numMessages = 5;
    std::vector<std::string> sentMessages;
    for (std::size_t i = 0; i < numMessages; ++i) {
        std::string msg = fmt::format("Batch Message #{}", i);
        sentMessages.push_back(msg);
        REQUIRE(outChannel.send(Common::Memory::ByteView(msg)));
    }

    std::array<std::array<std::byte, 256>, numMessages> buffers;
    std::array<::Backend::BufferSlot, numMessages> slots;
    for (std::size_t i = 0; i < numMessages; ++i) {
        slots[i] = ::Backend::BufferSlot { Common::Memory::MutableByteView(buffers[i]), 0 };
    }

    auto result = inChannel.receiveBatch(
        slots,
        std::chrono::milliseconds(500));

    REQUIRE(result.hasValue());
    REQUIRE(result.value() == numMessages);
    for (std::size_t i = 0; i < numMessages; ++i) {
        REQUIRE(slots[i].size == sentMessages[i].size());
        std::string receivedStr(reinterpret_cast<const char*>(buffers[i].data()), slots[i].size);
        CHECK(receivedStr == sentMessages[i]);
    }
}

TEST_CASE("Execution models handle oversized jumbo messages exceeding 2048 bytes", "[channel][core][jumbo]")
{
    class JumboVerifyHandler : public Core::Handler::IHandler {
    public:
        explicit JumboVerifyHandler(::Backend::IBackend& backend)
            : Core::Handler::IHandler(backend)
        {
        }

        void setRunner(Core::Handler::HandlerRunner* runner)
        {
            m_runner = runner;
        }

        Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override
        {
            std::lock_guard lock(m_mutex);
            lastReceivedChannel = std::string(channelName);
            lastReceivedSize = message.payload().size();
            lastReceivedPayload = std::string(reinterpret_cast<const char*>(message.payload().data()), message.payload().size());
            m_count++;
            if (message.payload().size() > 2048 && m_runner) {
                m_runner->stop();
            }
            return Core::Message::Consumed { };
        }

        std::mutex m_mutex;
        std::string lastReceivedChannel;
        std::size_t lastReceivedSize { 0 };
        std::string lastReceivedPayload;

    private:
        Core::Handler::HandlerRunner* m_runner { nullptr };
        int m_count { 0 };
    };

    SECTION("ThreadPool execution model receives oversized 4096-byte packet")
    {
        Core::Simulation::SimulationBackend backend;
        JumboVerifyHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(Core::Handler::ThreadPoolModel { .poolSize = 2 });
        handler.setRunner(&runner);

        // Generate a 4096-byte payload (exceeds 2048-byte default batch slot)
        std::string jumboPayload(4096, 'X');
        auto inChan = std::make_unique<MockInputChannel>("jumbo_in", std::vector<std::string> { jumboPayload });

        handler.registerInputChannel(std::move(inChan));
        runner.startAsync();
        runner.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 4096);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }

    SECTION("Dedicated thread model receives oversized 8192-byte packet")
    {
        Core::Simulation::SimulationBackend backend;
        JumboVerifyHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(Core::Handler::DedicatedThreadModel { });
        handler.setRunner(&runner);

        std::string jumboPayload(8192, 'Y');
        auto inChan = std::make_unique<MockInputChannel>("jumbo_in", std::vector<std::string> { jumboPayload });

        handler.registerInputChannel(std::move(inChan));
        runner.startAsync();
        runner.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 8192);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }

    SECTION("Sequential model receives oversized 4096-byte packet")
    {
        Core::Simulation::SimulationBackend backend;
        JumboVerifyHandler handler(backend);
        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionModel(Core::Handler::SequentialModel { });
        handler.setRunner(&runner);

        std::string jumboPayload(4096, 'Z');
        auto inChan = std::make_unique<MockInputChannel>("jumbo_in", std::vector<std::string> { jumboPayload });

        handler.registerInputChannel(std::move(inChan));
        runner.startAsync();
        runner.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 4096);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }
}

TEST_CASE("Output channels send multiple messages via vectorized sendBatch", "[channel][egress][batch]")
{
    const std::string sockPath = "/tmp/test_proxy_uds_egress_batch.sock";
    Proxy::Backend::Uds::UdsInputChannel inChannel("uds_in", sockPath);
    Proxy::Backend::Uds::UdsOutputChannel outChannel("uds_out", sockPath);

    std::vector<std::string> messages = { "Batch1", "Batch2", "Batch3", "Batch4" };
    std::vector<Common::Compat::span<const std::byte>> messageSpans;
    messageSpans.reserve(messages.size());
    for (const auto& msg : messages) {
        messageSpans.emplace_back(Common::Memory::ByteView(msg));
    }

    std::size_t sent = outChannel.sendBatch(
        Common::Compat::span<const Common::Compat::span<const std::byte>>(messageSpans.data(), messageSpans.size()));
    REQUIRE(sent == messages.size());

    std::array<std::array<std::byte, 64>, 4> recvBuffers;
    std::array<::Backend::BufferSlot, 4> slots;
    for (std::size_t i = 0; i < 4; ++i) {
        slots[i] = ::Backend::BufferSlot { Common::Memory::MutableByteView(recvBuffers[i]), 0 };
    }

    auto result = inChannel.receiveBatch(
        slots,
        std::chrono::milliseconds(500));

    REQUIRE(result.hasValue());
    REQUIRE(result.value() == messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        std::string recStr(reinterpret_cast<const char*>(recvBuffers[i].data()), slots[i].size);
        CHECK(recStr == messages[i]);
    }
}

TEST_CASE("ThreadPool execution model preserves strict FIFO message ordering per channel", "[channel][core][ordering]")
{
    class OrderVerifyHandler : public Core::Handler::IHandler {
    public:
        OrderVerifyHandler(::Backend::IBackend& backend, std::size_t expectedCount)
            : Core::Handler::IHandler(backend)
            , m_expectedCount(expectedCount)
        {
        }

        void setRunner(Core::Handler::HandlerRunner* runner)
        {
            m_runner = runner;
        }

        Core::Message::MessageResult onMessageReceived(std::string_view, const Core::Message::MessageView& message) override
        {
            std::lock_guard lock(m_mutex);
            received.emplace_back(reinterpret_cast<const char*>(message.payload().data()), message.payload().size());
            if (received.size() >= m_expectedCount && m_runner) {
                m_runner->stop();
            }
            return Core::Message::Consumed { };
        }

        std::mutex m_mutex;
        std::vector<std::string> received;

    private:
        Core::Handler::HandlerRunner* m_runner { nullptr };
        std::size_t m_expectedCount;
    };

    constexpr std::size_t kTotalMessages = 64;
    std::vector<std::string> expected;
    expected.reserve(kTotalMessages);
    for (std::size_t i = 0; i < kTotalMessages; ++i) {
        expected.push_back(fmt::format("seq_{}", i));
    }

    Core::Simulation::SimulationBackend backend;
    OrderVerifyHandler handler(backend, kTotalMessages);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::ThreadPoolModel { .poolSize = 4 });
    handler.setRunner(&runner);

    handler.registerInputChannel(std::make_unique<MockInputChannel>("ordered_chan", expected));

    runner.startAsync();
    runner.wait();

    REQUIRE(handler.received.size() == kTotalMessages);
    CHECK(handler.received == expected);
}
