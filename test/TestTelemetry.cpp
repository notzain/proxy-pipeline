#include "core/handler/IHandler.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "core/telemetry/Counter.hpp"
#include "core/telemetry/Gauge.hpp"
#include "core/telemetry/Histogram.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("Core::Telemetry::Counter atomic operations", "[telemetry]")
{
    Core::Telemetry::Counter counter("test_counter", "Test Counter Help");
    REQUIRE(counter.name() == "test_counter");
    REQUIRE(counter.help() == "Test Counter Help");
    REQUIRE(counter.value() == 0);

    counter.increment();
    REQUIRE(counter.value() == 1);

    counter.increment(99);
    REQUIRE(counter.value() == 100);

    // Multi-threaded concurrent increments
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&counter]() {
            for (int k = 0; k < 1000; ++k) {
                counter.increment();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    REQUIRE(counter.value() == 8100);

    counter.reset();
    REQUIRE(counter.value() == 0);
}

TEST_CASE("Core::Telemetry::Gauge operations", "[telemetry]")
{
    Core::Telemetry::Gauge gauge("test_gauge", "Test Gauge Help");
    REQUIRE(gauge.value() == 0);

    gauge.set(42);
    REQUIRE(gauge.value() == 42);

    gauge.add(10);
    REQUIRE(gauge.value() == 52);

    gauge.subtract(22);
    REQUIRE(gauge.value() == 30);
}

TEST_CASE("Core::Telemetry::Histogram observation and snapshot", "[telemetry]")
{
    const double bounds[] = { 10.0, 50.0, 100.0 };
    Core::Telemetry::Histogram hist("test_latency", "Latency in ms", bounds);

    hist.observe(5.0);
    hist.observe(20.0);
    hist.observe(75.0);
    hist.observe(120.0);

    REQUIRE(hist.count() == 4);
    REQUIRE(hist.sum() == 220.0);

    auto snap = hist.snapshot();
    REQUIRE(snap.count == 4);
    REQUIRE(snap.sum == 220.0);
    REQUIRE(snap.buckets.size() == 3);

    // Bucket <= 10.0 has 1 (5.0)
    REQUIRE(snap.buckets[0].first == 10.0);
    REQUIRE(snap.buckets[0].second == 1);

    // Bucket <= 50.0 has 2 (5.0, 20.0)
    REQUIRE(snap.buckets[1].first == 50.0);
    REQUIRE(snap.buckets[1].second == 2);

    // Bucket <= 100.0 has 3 (5.0, 20.0, 75.0)
    REQUIRE(snap.buckets[2].first == 100.0);
    REQUIRE(snap.buckets[2].second == 3);
}

TEST_CASE("Core::Telemetry::MetricsRegistry formatting to Prometheus format", "[telemetry]")
{
    auto& reg = Core::Telemetry::MetricsRegistry::instance();
    reg.clear();

    auto& c = reg.counter("http_requests_total", "Total requests received");
    c.increment(15);

    auto& g = reg.gauge("active_connections", "Number of active connections");
    g.set(7);

    const double bounds[] = { 50.0, 100.0 };
    auto& h = reg.histogram("request_duration_ms", "Request duration", bounds);
    h.observe(25.0);
    h.observe(80.0);

    std::string prom = reg.toPrometheus();
    REQUIRE(prom.find("http_requests_total 15") != std::string::npos);
    REQUIRE(prom.find("active_connections 7") != std::string::npos);
    REQUIRE(prom.find("request_duration_ms_count 2") != std::string::npos);
    REQUIRE(prom.find("request_duration_ms_bucket{le=\"50.000\"} 1") != std::string::npos);
    REQUIRE(prom.find("request_duration_ms_bucket{le=\"100.000\"} 2") != std::string::npos);
    REQUIRE(prom.find("request_duration_ms_bucket{le=\"+Inf\"} 2") != std::string::npos);
}

TEST_CASE("Core::Telemetry::StatsDExporter transmission", "[telemetry]")
{
    // Point to dummy UDP localhost port
    Core::Telemetry::StatsDExporter exporter("127.0.0.1", 8125, "proxy");

    exporter.sendCount("messages_received", 10);
    exporter.sendGauge("queue_depth", 5);
    exporter.sendTiming("processing_time", 1.25);

    // Test flush from registry
    auto& reg = Core::Telemetry::MetricsRegistry::instance();
    reg.clear();
    reg.counter("test_packets").increment(5);

    exporter.flushNow();
}

TEST_CASE("Core::Telemetry integration with IHandler pipeline execution", "[telemetry]")
{
    auto& reg = Core::Telemetry::MetricsRegistry::instance();
    reg.clear();

    class TelemetryTestHandler : public Core::Handler::IHandler {
    public:
        explicit TelemetryTestHandler(::Backend::IBackend& backend)
            : Core::Handler::IHandler(backend)
        {
        }

        std::atomic<std::size_t> receivedCount { 0 };

        Core::Message::MessageResult onMessageReceived(std::string_view, const Core::Message::MessageView&) override
        {
            receivedCount.fetch_add(1, std::memory_order_relaxed);
            return Core::Message::Consumed { };
        }
    };

    Core::Simulation::SimulationBackend backend;
    TelemetryTestHandler handler(backend);
    handler.setExecutionModel(Core::Handler::ExecutionModel::Sequential);

    // Create a mock envelope with ingress timestamp
    std::string payload = "Hello Telemetry";
    std::span<const std::byte> payloadSpan(reinterpret_cast<const std::byte*>(payload.data()), payload.size());
    uint64_t pastNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                              .count())
        - 50000; // 50 microseconds ago

    auto env = Core::Message::MessageEnvelope::packRaw(payloadSpan, pastNs);
    auto optView = Core::Message::MessageView::from(env);
    REQUIRE(optView.has_value());

    std::array<Core::Message::MessageView, 1> views { *optView };
    handler.processBatch("test_in", views);

    REQUIRE(handler.receivedCount.load() == 1);

    auto snap = reg.latencyHistogram("message_processing_duration_us").snapshot();
    REQUIRE(snap.count >= 1);
    REQUIRE(snap.sum > 0.0);
}
