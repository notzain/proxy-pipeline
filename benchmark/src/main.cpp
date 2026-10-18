#include "BenchmarkRunner.hpp"
#include "XmlVerificationHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
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
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "========================================================================================\n");
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "                XML VERIFICATION CHAIN END-TO-END BENCHMARK RESULTS                     \n");
    fmt::print(fmt::emphasis::bold | fg(fmt::color::cyan), "========================================================================================\n");
    fmt::print("{:<18} | {:<10} | {:<16} | {:<17} | {:<12} | {:<10}\n",
        "Strategy", "Messages", "Throughput (m/s)", "Throughput (MB/s)", "Avg Lat (µs)", "p99 (µs)");
    fmt::print("------------------------------------------------------------------------------------------\n");

    for (const auto& res : results) {
        fmt::print(fmt::emphasis::bold, "{:<18}", res.backendName);
        fmt::print(" | {:<10} | {:<16.2f} | {:<17.2f} | {:<12.2f} | {:<10.2f}\n",
            res.verifiedMessages,
            res.messagesPerSecond,
            res.throughputMBps,
            res.averageLatencyUs,
            res.p99LatencyUs);
    }
    fmt::print("========================================================================================\n\n");
}

} // namespace

int main(int argc, char* argv[])
{
    std::size_t numMessages = 10000;
    if (argc > 1) {
        numMessages = std::stoul(argv[1]);
    }

    std::filesystem::path xsdPath = "/tmp/benchmark_order.xsd";
    createTempXsd(xsdPath);

    std::vector<Proxy::Benchmark::BenchmarkResult> results;

    // --- 1. Direct Pipeline / In-Memory Channel Verification Benchmark ---
    {
        fmt::print(fg(fmt::color::yellow), "[Benchmark] Running direct XML verification on {} messages...\n", numMessages);
        Proxy::Handler::XmlVerificationHandler verifier(xsdPath, "output");

        std::span<const std::byte> msgSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());

        std::vector<double> latenciesUs;
        latenciesUs.reserve(numMessages);

        auto startAll = std::chrono::steady_clock::now();
        std::size_t verified = 0;

        for (std::size_t i = 0; i < numMessages; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            if (verifier.validate(msgSpan)) {
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
    }

    // Helper lambda to run Socket (UDP) benchmark with a specific execution strategy
    auto runSocketBenchmark = [&](Core::ExecutionModel model, std::string_view strategyName, std::size_t poolSize = 0) {
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

        Core::Configuration config = Core::Configuration::fromFile("/tmp/bench_socket.conf");
        std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
        backend->configure(config);

        // Instantiate components
        auto verifierIn = backend->createInputChannel("udp_raw", config);
        auto verifierOut = backend->createOutputChannel("udp_verified", config);
        Proxy::Handler::XmlVerificationHandler verifier(xsdPath, "udp_verified");
        verifier.setExecutionModel(model, poolSize);
        verifier.registerInputChannel("udp_raw", std::move(verifierIn));
        verifier.registerOutputChannel("udp_verified", std::move(verifierOut));

        auto receiverIn = backend->createInputChannel("udp_verified", config);
        Proxy::Benchmark::BenchmarkReceiver receiver(socketMessages);
        receiver.setExecutionModel(model, poolSize);
        receiver.registerInputChannel("udp_verified", std::move(receiverIn));

        auto senderOut = backend->createOutputChannel("udp_raw", config);

        // Start verifier and receiver concurrently
        verifier.startAsync();
        receiver.startAsync();

        // Brief delay to allow UDP socket binding
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        std::span<const std::byte> msgSpan(reinterpret_cast<const std::byte*>(validXmlMsg.data()), validXmlMsg.size());

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

        // Wait up to 5 seconds for messages to arrive
        receiver.waitForCompletion(std::chrono::seconds(5));
        receiver.stop();
        receiver.wait();

        auto endAll = std::chrono::steady_clock::now();
        verifier.stop();
        verifier.wait();

        auto totalDuration = endAll - startAll;
        double sec = std::chrono::duration<double>(totalDuration).count();
        const auto& recvTimestamps = receiver.getReceiveTimestamps();

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

    // Benchmark across execution strategies
    runSocketBenchmark(Core::ExecutionModel::Dedicated, "UDP (Dedicated)");
    runSocketBenchmark(Core::ExecutionModel::Sequential, "UDP (Sequential)");
    runSocketBenchmark(Core::ExecutionModel::ThreadPool, "UDP (ThreadPool 4)", 4);

    // Print summary table
    printResultTable(results);

    std::error_code ec;
    std::filesystem::remove(xsdPath, ec);
    return 0;
}
