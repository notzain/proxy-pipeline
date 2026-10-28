#include "common/memory/ByteView.hpp"
#include "core/simulation/PcapWriter.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <fmt/core.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::string outputFile = (argc > 1) ? argv[1] : "sample_traffic.pcap";
    int count = (argc > 2) ? std::stoi(argv[2]) : 20;

    Core::Simulation::PcapWriter writer;
    if (!writer.open(outputFile, true)) {
        fmt::print(stderr, "Failed to open PCAP output file: {}\n", outputFile);
        return 1;
    }

    std::uint32_t clientIp = inet_addr("192.168.1.100");
    std::uint32_t proxyIp = inet_addr("127.0.0.1");
    std::uint16_t clientPort = htons(54321);
    std::uint16_t proxyPort = htons(9123);

    auto baseTimeNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    fmt::print("Generating {} simulated UDP packets into '{}'...\n", count, outputFile);

    for (int i = 1; i <= count; ++i) {
        std::string payload;
        if (i % 5 == 0) {
            // Malformed XML to test rejection
            payload = fmt::format("<order><customer>Customer_{}</customer><item>Widget-{}</item><bad_element>invalid</bad_element></order>", i, i);
        } else {
            // Valid compliant XML
            payload = fmt::format("<order><customer>Customer_{}</customer><item>Widget-{}</item><quantity>{}</quantity></order>", i, i, i * 10);
        }

        std::uint64_t packetTimeNs = baseTimeNs + (i * 20'000'000ULL); // 20ms apart

        writer.writeUdpPacket(clientIp, proxyIp, clientPort, proxyPort, Common::Memory::ByteView(payload), packetTimeNs);
    }

    writer.close();
    fmt::print("Successfully generated '{}' ({} packets).\n", outputFile, count);
    return 0;
}
