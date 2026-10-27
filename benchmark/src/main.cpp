#include "BenchmarkRunner.hpp"
#include "XmlVerificationHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/inproc/InProcBackend.hpp"
#include "backend/shm/ShmBackend.hpp"
#include "backend/socket/SocketBackend.hpp"
#include "backend/uds/UdsBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "log/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

const std::string xsdContent = R"(<?xml version="1.0" encoding="UTF-8"?>
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
    <xs:element name="order">
        <xs:complexType>
            <xs:sequence>
                <xs:element name="customer" type="xs:string"/>
                <xs:element name="item" type="xs:string"/>
                <xs:element name="quantity" type="xs:positiveInteger"/>
            </xs:sequence>
        </xs:complexType>
    </xs:element>
</xs:schema>
)";

const std::string validXmlMsg = "<order><customer>Alice</customer><item>Widget</item><quantity>5</quantity></order>";

void createTempXsd(const std::filesystem::path& path)
{
    std::ofstream out(path);
    out << xsdContent;
}

void printResultTable(const std::vector<Proxy::Benchmark::BenchmarkResult>& results)
{
    fmt::print("\n");
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "====================================================================================================\n");
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "                        XML VERIFICATION CHAIN END-TO-END BENCHMARK RESULTS                         \n");
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "====================================================================================================\n");
    fmt::print("{:<22} | {:<10} | {:<16} | {:<17} | {:<12} | {:<10}\n",
        "Strategy", "Messages", "Throughput (m/s)", "Throughput (MB/s)", "Avg Lat (µs)", "p99 (µs)");
    fmt::print("----------------------------------------------------------------------------------------------------\n");

    for (const auto& res : results) {
        fmt::print(fmt::emphasis::bold, "{:<22}", res.backendName);
        fmt::print(" | {:<10} | {:<16.2f} | {:<17.2f} | {:<12.2f} | {:<10.2f}\n",
            res.verifiedMessages,
            res.messagesPerSecond,
            res.throughputMBps,
            res.averageLatencyUs,
            res.p99LatencyUs);
    }
    fmt::print("====================================================================================================\n\n");
}

} // namespace

int main(int argc, char* argv[])
{
    std::size_t numMessages = 10000;
    std::size_t iterations = 1;
    std::string targetStrategy = "all";
    bool strictFifo = false; // default false for benchmarks to allow full parallel queue utilization

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            fmt::print("Usage: proxy_benchmark [options]\n");
            fmt::print("Options:\n");
            fmt::print("  --messages <N>        Number of messages to send (default: 10000)\n");
            fmt::print("  --iterations <N>      Number of benchmark runs (default: 1)\n");
            fmt::print("  --strategy <NAME>     Strategy to run: all, direct, dedicated, sequential, threadpool4, threadpool8 (default: all)\n");
            fmt::print("  --strict-fifo         Enforce per-flow/channel strict FIFO ordering in ThreadPool (default: false)\n");
            fmt::print("  --help, -h            Show this help message\n");
            return 0;
        } else if (arg == "--messages" && i + 1 < argc) {
            numMessages = std::stoul(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::stoul(argv[++i]);
        } else if (arg == "--strategy" && i + 1 < argc) {
            targetStrategy = argv[++i];
        } else if (arg == "--strict-fifo") {
            strictFifo = true;
        } else if (arg == "--no-strict-fifo") {
            strictFifo = false;
        } else if (arg.rfind("--messages=", 0) == 0) {
            numMessages = std::stoul(std::string(arg.substr(11)));
        } else if (arg.rfind("--iterations=", 0) == 0) {
            iterations = std::stoul(std::string(arg.substr(13)));
        } else if (arg.rfind("--strategy=", 0) == 0) {
            targetStrategy = std::string(arg.substr(11));
        } else {
            // Positional fallback for backward compatibility: proxy_benchmark <messages>
            try {
                numMessages = std::stoul(argv[i]);
            } catch (...) {
                fmt::print(stderr, "Unknown option or invalid value: {}\n", arg);
                return 1;
            }
        }
    }

    std::filesystem::path xsdPath = "/tmp/benchmark_order.xsd";
    createTempXsd(xsdPath);

    std::vector<Proxy::Benchmark::BenchmarkResult> results;

    // Helper lambda to run Direct benchmark
    auto runDirectBenchmark = [&]() {
        fmt::print(fg(fmt::color::yellow), "[Benchmark] Running direct XML verification on {} messages...\n", numMessages);
        Proxy::Backend::Socket::SocketBackend backend;
        Proxy::Handler::XmlVerificationHandler verifier(backend, xsdPath);

        Common::Compat::span<const std::byte> rawXmlSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());
        auto envelope = Core::Message::MessageEnvelope::packRaw(rawXmlSpan);
        Common::Compat::span<const std::byte> msgSpan(envelope.data(), envelope.size());

        std::vector<double> latenciesUs;
        latenciesUs.reserve(numMessages);

        auto startAll = std::chrono::steady_clock::now();
        std::size_t verified = 0;

        for (std::size_t i = 0; i < numMessages; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            if (verifier.validate(rawXmlSpan)) {
                verified++;
            }
            auto t1 = std::chrono::steady_clock::now();
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto endAll = std::chrono::steady_clock::now();

        std::sort(latenciesUs.begin(), latenciesUs.end());
        double totalLat = 0;
        for (double l : latenciesUs) {
            totalLat += l;
        }

        auto totalDuration = endAll - startAll;
        double sec = std::chrono::duration<double>(totalDuration).count();

        Proxy::Benchmark::BenchmarkResult res;
        res.backendName = "Direct (XSD)";
        res.totalMessages = numMessages;
        res.verifiedMessages = verified;
        res.totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(totalDuration);
        res.messagesPerSecond = verified / sec;
        res.averageLatencyUs = totalLat / latenciesUs.size();
        res.medianLatencyUs = latenciesUs[latenciesUs.size() / 2];
        res.p95LatencyUs = latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.95)];
        res.p99LatencyUs = latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.99)];
        res.throughputMBps = (verified * validXmlMsg.size()) / (sec * 1024.0 * 1024.0);

        results.push_back(res);
    };

    // Helper lambda to run Socket (UDP) benchmark with a specific execution strategy
    auto runSocketBenchmark = [&](const Core::Handler::ExecutionModel& model, std::string_view strategyName) {
        std::size_t socketMessages = numMessages;
        fmt::print(fg(fmt::color::yellow), "[Benchmark] Running Socket (UDP) chain ({}) with {} messages...\n", strategyName, socketMessages);

        std::ofstream confFile("/tmp/bench_socket.conf");
        confFile << "socket.host=127.0.0.1\n";
        confFile << "socket.port=9870\n";
        confFile << "channel.udp_raw.host=127.0.0.1\n";
        confFile << "channel.udp_raw.port=9871\n";
        confFile << "channel.udp_raw.dest_host=127.0.0.1\n";
        confFile << "channel.udp_raw.dest_port=9871\n";
        confFile << "channel.udp_verified.host=127.0.0.1\n";
        confFile << "channel.udp_verified.port=9872\n";
        confFile << "channel.udp_verified.dest_host=127.0.0.1\n";
        confFile << "channel.udp_verified.dest_port=9872\n";
        confFile << "xml.xsd_path=" << xsdPath.string() << "\n";
        confFile << "xml.input_channel=udp_raw\n";
        confFile << "xml.output_channel=udp_verified\n";
        confFile.close();

        Core::Config::Configuration config = Core::Config::Configuration::fromFile("/tmp/bench_socket.conf");
        std::unique_ptr<::Backend::IBackend> backend = std::make_unique<Proxy::Backend::Socket::SocketBackend>();
        backend->configure(config);

        // Instantiate components
        auto verifierIn = backend->createInputChannel("udp_raw", config);
        auto verifierOut = backend->createOutputChannel("udp_verified", config);
        Proxy::Handler::XmlVerificationHandler verifier(*backend, xsdPath);
        Core::Handler::HandlerRunner verifierRunner(verifier);
        verifierRunner.setExecutionModel(model);
        verifier.registerInputChannel(std::move(verifierIn));
        verifier.setOutputChannel(verifier.registerOutputChannel(std::move(verifierOut)));

        auto receiverIn = backend->createInputChannel("udp_verified", config);
        Proxy::Benchmark::BenchmarkReceiver receiver(*backend, socketMessages);
        Core::Handler::HandlerRunner receiverRunner(receiver);
        receiverRunner.setExecutionModel(model);
        receiver.registerInputChannel(std::move(receiverIn));

        auto senderOut = backend->createOutputChannel("udp_raw", config);

        // Start verifier and receiver concurrently
        verifierRunner.startAsync(true);
        receiverRunner.startAsync(false);

        // Brief delay to allow UDP socket binding
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        Common::Compat::span<const std::byte> rawXmlSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());
        auto envelope = Core::Message::MessageEnvelope::packRaw(rawXmlSpan);
        Common::Compat::span<const std::byte> msgSpan(envelope.data(), envelope.size());

        auto startAll = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> sendTimestamps;
        sendTimestamps.reserve(socketMessages);

        for (std::size_t i = 0; i < socketMessages; ++i) {
            sendTimestamps.push_back(std::chrono::steady_clock::now());
            senderOut->send(msgSpan);
            if ((i & 0x7F) == 0) { // Slight yield every 128 packets to prevent OS socket buffer overrun
                std::this_thread::yield();
            }
        }

        // Wait for messages to arrive (dynamically scaled with message count)
        receiver.waitForCompletion(std::chrono::seconds(std::max<long>(5, static_cast<long>(numMessages / 5000 + 5))));
        receiverRunner.stop();
        receiverRunner.wait();

        auto endAll = std::chrono::steady_clock::now();
        verifierRunner.stop();
        verifierRunner.wait();

        const auto& recvTimestamps = receiver.getReceiveTimestamps();
        auto totalDuration = (!recvTimestamps.empty() && recvTimestamps.size() < numMessages)
            ? (recvTimestamps.back() - startAll)
            : (endAll - startAll);
        if (totalDuration.count() <= 0) {
            totalDuration = endAll - startAll;
        }
        double sec = std::chrono::duration<double>(totalDuration).count();

        std::vector<double> latenciesUs;
        std::size_t count = std::min(sendTimestamps.size(), recvTimestamps.size());
        for (std::size_t i = 0; i < count; ++i) {
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(recvTimestamps[i] - sendTimestamps[i]).count());
        }
        std::sort(latenciesUs.begin(), latenciesUs.end());

        double totalLat = 0;
        for (double l : latenciesUs) {
            totalLat += l;
        }

        Proxy::Benchmark::BenchmarkResult res;
        res.backendName = std::string(strategyName);
        res.totalMessages = socketMessages;
        res.verifiedMessages = count;
        res.totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(totalDuration);
        res.messagesPerSecond = count / sec;
        res.averageLatencyUs = latenciesUs.empty() ? 0 : totalLat / latenciesUs.size();
        res.medianLatencyUs = latenciesUs.empty() ? 0 : latenciesUs[latenciesUs.size() / 2];
        res.p95LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.95)];
        res.p99LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.99)];
        res.throughputMBps = (count * validXmlMsg.size()) / (sec * 1024.0 * 1024.0);

        results.push_back(res);

        // Small pause between benchmark runs so OS releases sockets
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };

    // Helper lambda to run IPC (UDS) benchmark with a specific execution strategy
    auto runUdsBenchmark = [&](const Core::Handler::ExecutionModel& model, std::string_view strategyName) {
        std::size_t udsMessages = numMessages;
        fmt::print(fg(fmt::color::cyan), "[Benchmark] Running IPC (UDS) chain ({}) with {} messages...\n", strategyName, udsMessages);

        std::ofstream confFile("/tmp/bench_uds.conf");
        confFile << "channel.uds_raw.path=/tmp/bench_uds_raw.sock\n";
        confFile << "channel.uds_raw.dest_path=/tmp/bench_uds_raw.sock\n";
        confFile << "channel.uds_verified.path=/tmp/bench_uds_verified.sock\n";
        confFile << "channel.uds_verified.dest_path=/tmp/bench_uds_verified.sock\n";
        confFile << "xml.xsd_path=" << xsdPath.string() << "\n";
        confFile << "xml.input_channel=uds_raw\n";
        confFile << "xml.output_channel=uds_verified\n";
        confFile.close();

        Core::Config::Configuration config = Core::Config::Configuration::fromFile("/tmp/bench_uds.conf");
        std::unique_ptr<::Backend::IBackend> backend = std::make_unique<Proxy::Backend::Uds::UdsBackend>();
        backend->configure(config);

        // Instantiate components: create receiver's input channel first so its socket path is bound
        auto receiverIn = backend->createInputChannel("uds_verified", config);
        Proxy::Benchmark::BenchmarkReceiver receiver(*backend, udsMessages);
        Core::Handler::HandlerRunner receiverRunner(receiver);
        receiverRunner.setExecutionModel(model);
        receiver.registerInputChannel(std::move(receiverIn));

        auto verifierIn = backend->createInputChannel("uds_raw", config);
        auto verifierOut = backend->createOutputChannel("uds_verified", config);
        Proxy::Handler::XmlVerificationHandler verifier(*backend, xsdPath);
        Core::Handler::HandlerRunner verifierRunner(verifier);
        verifierRunner.setExecutionModel(model);
        verifier.registerInputChannel(std::move(verifierIn));
        verifier.setOutputChannel(verifier.registerOutputChannel(std::move(verifierOut)));

        auto senderOut = backend->createOutputChannel("uds_raw", config);

        // Start verifier and receiver concurrently
        verifierRunner.startAsync(true);
        receiverRunner.startAsync(false);

        // Brief delay to allow UDS socket binding
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        Common::Compat::span<const std::byte> rawXmlSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());
        auto envelope = Core::Message::MessageEnvelope::packRaw(rawXmlSpan);
        Common::Compat::span<const std::byte> msgSpan(envelope.data(), envelope.size());

        auto startAll = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> sendTimestamps;
        sendTimestamps.reserve(udsMessages);

        for (std::size_t i = 0; i < udsMessages; ++i) {
            sendTimestamps.push_back(std::chrono::steady_clock::now());
            senderOut->send(msgSpan);
            if ((i & 0x7F) == 0) {
                std::this_thread::yield();
            }
        }

        // Wait for messages to arrive (dynamically scaled with message count)
        receiver.waitForCompletion(std::chrono::seconds(std::max<long>(5, static_cast<long>(numMessages / 5000 + 5))));
        receiverRunner.stop();
        receiverRunner.wait();

        auto endAll = std::chrono::steady_clock::now();
        verifierRunner.stop();
        verifierRunner.wait();

        const auto& recvTimestamps = receiver.getReceiveTimestamps();
        auto totalDuration = (!recvTimestamps.empty() && recvTimestamps.size() < numMessages)
            ? (recvTimestamps.back() - startAll)
            : (endAll - startAll);
        if (totalDuration.count() <= 0) {
            totalDuration = endAll - startAll;
        }
        double sec = std::chrono::duration<double>(totalDuration).count();

        std::vector<double> latenciesUs;
        std::size_t count = std::min(sendTimestamps.size(), recvTimestamps.size());
        for (std::size_t i = 0; i < count; ++i) {
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(recvTimestamps[i] - sendTimestamps[i]).count());
        }
        std::sort(latenciesUs.begin(), latenciesUs.end());

        double totalLat = 0;
        for (double l : latenciesUs) {
            totalLat += l;
        }

        Proxy::Benchmark::BenchmarkResult res;
        res.backendName = std::string(strategyName);
        res.totalMessages = udsMessages;
        res.verifiedMessages = count;
        res.totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(totalDuration);
        res.messagesPerSecond = count / sec;
        res.averageLatencyUs = latenciesUs.empty() ? 0 : totalLat / latenciesUs.size();
        res.medianLatencyUs = latenciesUs.empty() ? 0 : latenciesUs[latenciesUs.size() / 2];
        res.p95LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.95)];
        res.p99LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.99)];
        res.throughputMBps = (count * validXmlMsg.size()) / (sec * 1024.0 * 1024.0);

        results.push_back(res);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };

    // Helper lambda to run Kernel Bypass IPC (SHM) benchmark with a specific execution strategy
    auto runShmBenchmark = [&](const Core::Handler::ExecutionModel& model, std::string_view strategyName) {
        std::size_t shmMessages = numMessages;
        fmt::print(fg(fmt::color::cyan), "[Benchmark] Running Kernel Bypass IPC (SHM) chain ({}) with {} messages...\n", strategyName, shmMessages);

        std::ofstream confFile("/tmp/bench_shm.conf");
        confFile << "channel.shm_raw.path=/tmp/bench_shm_raw.dat\n";
        confFile << "channel.shm_raw.dest_path=/tmp/bench_shm_raw.dat\n";
        confFile << "channel.shm_raw.capacity=4096\n";
        confFile << "channel.shm_verified.path=/tmp/bench_shm_verified.dat\n";
        confFile << "channel.shm_verified.dest_path=/tmp/bench_shm_verified.dat\n";
        confFile << "channel.shm_verified.capacity=4096\n";
        confFile << "xml.xsd_path=" << xsdPath.string() << "\n";
        confFile << "xml.input_channel=shm_raw\n";
        confFile << "xml.output_channel=shm_verified\n";
        confFile.close();

        std::filesystem::remove("/tmp/bench_shm_raw.dat");
        std::filesystem::remove("/tmp/bench_shm_verified.dat");

        Core::Config::Configuration config = Core::Config::Configuration::fromFile("/tmp/bench_shm.conf");
        std::unique_ptr<::Backend::IBackend> backend = std::make_unique<Proxy::Backend::Shm::ShmBackend>();
        backend->configure(config);

        auto receiverIn = backend->createInputChannel("shm_verified", config);
        Proxy::Benchmark::BenchmarkReceiver receiver(*backend, shmMessages);
        Core::Handler::HandlerRunner receiverRunner(receiver);
        receiverRunner.setExecutionModel(model);
        receiver.registerInputChannel(std::move(receiverIn));

        auto verifierIn = backend->createInputChannel("shm_raw", config);
        auto verifierOut = backend->createOutputChannel("shm_verified", config);
        Proxy::Handler::XmlVerificationHandler verifier(*backend, xsdPath);
        Core::Handler::HandlerRunner verifierRunner(verifier);
        verifierRunner.setExecutionModel(model);
        verifier.registerInputChannel(std::move(verifierIn));
        verifier.setOutputChannel(verifier.registerOutputChannel(std::move(verifierOut)));

        auto senderOut = backend->createOutputChannel("shm_raw", config);

        verifierRunner.startAsync(true);
        receiverRunner.startAsync(false);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        Common::Compat::span<const std::byte> rawXmlSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());
        auto envelope = Core::Message::MessageEnvelope::packRaw(rawXmlSpan);
        Common::Compat::span<const std::byte> msgSpan(envelope.data(), envelope.size());

        auto startAll = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> sendTimestamps;
        sendTimestamps.reserve(shmMessages);

        std::size_t sentCount = 0;
        for (std::size_t i = 0; i < shmMessages; ++i) {
            sendTimestamps.push_back(std::chrono::steady_clock::now());
            if (senderOut->send(msgSpan)) {
                ++sentCount;
            }
            if ((i & 0x7F) == 0) {
                std::this_thread::yield();
            }
        }
        if (sentCount < shmMessages) {
            fmt::print(fg(fmt::color::yellow), "[Benchmark] Warning: only {}/{} messages sent successfully\n", sentCount, shmMessages);
        }

        receiver.waitForCompletion(
            std::chrono::seconds(std::max<long>(5, static_cast<long>(numMessages / 5000 + 5))),
            std::chrono::milliseconds(2000));
        receiverRunner.stop();
        receiverRunner.wait();

        auto endAll = std::chrono::steady_clock::now();
        verifierRunner.stop();
        verifierRunner.wait();

        const auto& recvTimestamps = receiver.getReceiveTimestamps();
        auto totalDuration = (!recvTimestamps.empty() && recvTimestamps.size() < numMessages)
            ? (recvTimestamps.back() - startAll)
            : (endAll - startAll);
        if (totalDuration.count() <= 0) {
            totalDuration = endAll - startAll;
        }
        double sec = std::chrono::duration<double>(totalDuration).count();

        std::vector<double> latenciesUs;
        std::size_t count = std::min(sendTimestamps.size(), recvTimestamps.size());
        for (std::size_t i = 0; i < count; ++i) {
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(recvTimestamps[i] - sendTimestamps[i]).count());
        }
        std::sort(latenciesUs.begin(), latenciesUs.end());

        double totalLat = 0;
        for (double l : latenciesUs) {
            totalLat += l;
        }

        Proxy::Benchmark::BenchmarkResult res;
        res.backendName = std::string(strategyName);
        res.totalMessages = shmMessages;
        res.verifiedMessages = count;
        res.totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(totalDuration);
        res.messagesPerSecond = count / sec;
        res.averageLatencyUs = latenciesUs.empty() ? 0 : totalLat / latenciesUs.size();
        res.medianLatencyUs = latenciesUs.empty() ? 0 : latenciesUs[latenciesUs.size() / 2];
        res.p95LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.95)];
        res.p99LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.99)];
        res.throughputMBps = (count * validXmlMsg.size()) / (sec * 1024.0 * 1024.0);

        results.push_back(res);

        std::filesystem::remove("/tmp/bench_shm_raw.dat");
        std::filesystem::remove("/tmp/bench_shm_verified.dat");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };

    // Helper lambda to run In-Process Pipeline benchmark
    auto runInProcBenchmark = [&](const Core::Handler::ExecutionModel& model, std::string_view strategyName) {
        std::size_t inprocMessages = numMessages;
        fmt::print(fg(fmt::color::green), "[Benchmark] Running In-Process Pipeline chain ({}) with {} messages...\n", strategyName, inprocMessages);

        Core::Config::Configuration config;
        Proxy::Backend::InProc::InProcBackend backend;
        backend.configure(config);

        auto receiverIn = backend.createInputChannel("inproc_verified", config);
        Proxy::Benchmark::BenchmarkReceiver receiver(backend, inprocMessages);
        Core::Handler::HandlerRunner receiverRunner(receiver);
        receiverRunner.setExecutionModel(model);
        receiver.registerInputChannel(std::move(receiverIn));

        auto verifierIn = backend.createInputChannel("inproc_raw", config);
        auto verifierOut = backend.createOutputChannel("inproc_verified", config);
        Proxy::Handler::XmlVerificationHandler verifier(backend, xsdPath);
        Core::Handler::HandlerRunner verifierRunner(verifier);
        verifierRunner.setExecutionModel(model);
        verifier.registerInputChannel(std::move(verifierIn));
        verifier.setOutputChannel(verifier.registerOutputChannel(std::move(verifierOut)));

        auto senderOut = backend.createOutputChannel("inproc_raw", config);

        verifierRunner.startAsync(true);
        receiverRunner.startAsync(false);

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        Common::Compat::span<const std::byte> rawXmlSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());
        auto envelope = Core::Message::MessageEnvelope::packRaw(rawXmlSpan);
        Common::Compat::span<const std::byte> msgSpan(envelope.data(), envelope.size());

        auto startAll = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> sendTimestamps;
        sendTimestamps.reserve(inprocMessages);

        std::size_t sentCount = 0;
        for (std::size_t i = 0; i < inprocMessages; ++i) {
            sendTimestamps.push_back(std::chrono::steady_clock::now());
            if (senderOut->send(msgSpan)) {
                ++sentCount;
            }
            if ((i & 0x1FF) == 0) {
                std::this_thread::yield();
            }
        }
        if (sentCount < inprocMessages) {
            fmt::print(fg(fmt::color::yellow), "[Benchmark] Warning: only {}/{} messages sent successfully\n", sentCount, inprocMessages);
        }

        receiver.waitForCompletion(
            std::chrono::seconds(std::max<long>(5, static_cast<long>(numMessages / 5000 + 5))),
            std::chrono::milliseconds(500));
        receiverRunner.stop();
        receiverRunner.wait();

        auto endAll = std::chrono::steady_clock::now();
        verifierRunner.stop();
        verifierRunner.wait();

        const auto& recvTimestamps = receiver.getReceiveTimestamps();
        auto totalDuration = (!recvTimestamps.empty() && recvTimestamps.size() < numMessages)
            ? (recvTimestamps.back() - startAll)
            : (endAll - startAll);
        if (totalDuration.count() <= 0) {
            totalDuration = endAll - startAll;
        }
        double sec = std::chrono::duration<double>(totalDuration).count();

        std::vector<double> latenciesUs;
        std::size_t count = std::min(sendTimestamps.size(), recvTimestamps.size());
        for (std::size_t i = 0; i < count; ++i) {
            latenciesUs.push_back(std::chrono::duration<double, std::micro>(recvTimestamps[i] - sendTimestamps[i]).count());
        }
        std::sort(latenciesUs.begin(), latenciesUs.end());

        double totalLat = 0;
        for (double l : latenciesUs) {
            totalLat += l;
        }

        Proxy::Benchmark::BenchmarkResult res;
        res.backendName = std::string(strategyName);
        res.totalMessages = inprocMessages;
        res.verifiedMessages = count;
        res.totalDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(totalDuration);
        res.messagesPerSecond = count / sec;
        res.averageLatencyUs = latenciesUs.empty() ? 0 : totalLat / latenciesUs.size();
        res.medianLatencyUs = latenciesUs.empty() ? 0 : latenciesUs[latenciesUs.size() / 2];
        res.p95LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.95)];
        res.p99LatencyUs = latenciesUs.empty() ? 0 : latenciesUs[static_cast<std::size_t>(latenciesUs.size() * 0.99)];
        res.throughputMBps = (count * validXmlMsg.size()) / (sec * 1024.0 * 1024.0);

        results.push_back(res);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };

    auto runSelected = [&](std::string_view strategy) {
        if (strategy == "all" || strategy == "direct") {
            runDirectBenchmark();
        }
        if (strategy == "all" || strategy == "udp-dedicated" || strategy == "dedicated") {
            runSocketBenchmark(Core::Handler::DedicatedThreadModel { }, "UDP (Dedicated)");
        }
        if (strategy == "all" || strategy == "uds-dedicated") {
            runUdsBenchmark(Core::Handler::DedicatedThreadModel { }, "UDS (Dedicated)");
        }
        if (strategy == "all" || strategy == "shm-dedicated" || strategy == "shm") {
            runShmBenchmark(Core::Handler::DedicatedThreadModel { }, "SHM (Dedicated)");
        }
        if (strategy == "all" || strategy == "udp-sequential" || strategy == "sequential") {
            runSocketBenchmark(Core::Handler::SequentialModel { }, "UDP (Sequential)");
        }
        if (strategy == "all" || strategy == "uds-sequential") {
            runUdsBenchmark(Core::Handler::SequentialModel { }, "UDS (Sequential)");
        }
        if (strategy == "all" || strategy == "shm-sequential" || strategy == "shm") {
            runShmBenchmark(Core::Handler::SequentialModel { }, "SHM (Sequential)");
        }
        if (strategy == "all" || strategy == "udp-threadpool4" || strategy == "threadpool4") {
            runSocketBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 4, .strictFifo = strictFifo }, "UDP (ThreadPool 4)");
        }
        if (strategy == "all" || strategy == "uds-threadpool4") {
            runUdsBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 4, .strictFifo = strictFifo }, "UDS (ThreadPool 4)");
        }
        if (strategy == "all" || strategy == "shm-threadpool4" || strategy == "shm") {
            runShmBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 4, .strictFifo = strictFifo }, "SHM (ThreadPool 4)");
        }
        if (strategy == "all" || strategy == "udp-threadpool8" || strategy == "threadpool8") {
            runSocketBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 8, .strictFifo = strictFifo }, "UDP (ThreadPool 8)");
        }
        if (strategy == "all" || strategy == "uds-threadpool8") {
            runUdsBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 8, .strictFifo = strictFifo }, "UDS (ThreadPool 8)");
        }
        if (strategy == "all" || strategy == "shm-threadpool8" || strategy == "shm") {
            runShmBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 8, .strictFifo = strictFifo }, "SHM (ThreadPool 8)");
        }
        if (strategy == "all" || strategy == "inproc" || strategy == "inproc-dedicated") {
            runInProcBenchmark(Core::Handler::DedicatedThreadModel { }, "InProc (Dedicated)");
        }
        if (strategy == "all" || strategy == "inproc" || strategy == "inproc-sequential") {
            runInProcBenchmark(Core::Handler::SequentialModel { }, "InProc (Sequential)");
        }
        if (strategy == "all" || strategy == "inproc" || strategy == "inproc-threadpool4") {
            runInProcBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 4, .strictFifo = strictFifo }, "InProc (ThreadPool 4)");
        }
        if (strategy == "all" || strategy == "inproc" || strategy == "inproc-threadpool8") {
            runInProcBenchmark(Core::Handler::ThreadPoolModel { .poolSize = 8, .strictFifo = strictFifo }, "InProc (ThreadPool 8)");
        }
    };

    for (std::size_t it = 0; it < iterations; ++it) {
        if (iterations > 1) {
            fmt::print(fmt::emphasis::bold | fg(fmt::color::magenta), "\n--- Iteration {}/{} ---\n", it + 1, iterations);
        }
        runSelected(targetStrategy);
    }

    // Print summary table
    printResultTable(results);

    std::error_code ec;
    std::filesystem::remove(xsdPath, ec);
    return 0;
}
