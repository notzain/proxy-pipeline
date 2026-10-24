#include "backend/sim/SimBackend.hpp"
#include "SimInputChannel.hpp"
#include "SimOutputChannel.hpp"
#include "log/ConsoleLogSink.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <memory>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <variant>

namespace {

std::string escapeJsonString(std::string_view input, std::size_t maxLen)
{
    std::size_t limit = std::min(input.size(), maxLen);
    std::string out;
    out.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        char c = input[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += (c >= 32 && c <= 126) ? c : '.';
        }
    }
    return out;
}

} // namespace

namespace Proxy::Backend::Sim {

SimBackend::SimBackend()
{
    m_unixEventFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    m_udpEventFd = ::socket(AF_INET, SOCK_DGRAM, 0);
}

SimBackend::~SimBackend()
{
    if (m_unixEventFd >= 0) {
        ::close(m_unixEventFd);
    }
    if (m_udpEventFd >= 0) {
        ::close(m_udpEventFd);
    }
}

void SimBackend::configure(const ::Backend::IConfig& config)
{
    m_pcapPath = config.getOr<std::string>("pcap.path", config.getOr<std::string>("sim.pcap", ""));
    m_telemetryHost = config.getOr<std::string>("sim.event_host", "127.0.0.1");
    m_telemetryPort = config.getOr<int>("sim.event_port", 8092);
    m_eventSockPath = config.getOr<std::string>("sim.event_sock", "/tmp/proxy_sim_events.sock");
    m_handlerName = config.getOr<std::string>("sim.handler_name", config.getOr<std::string>("telemetry.prefix", "handler"));

    bool logConsole = config.getOr<bool>("log.console", true);
    if (logConsole) {
        Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
    }
}

std::unique_ptr<::Backend::IInputChannel> SimBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig& config)
{
    std::string defaultSock = fmt::format("/tmp/proxy_sim_{}.sock", name);
    std::string sockPath = config.getOr<std::string>(fmt::format("channel.{}.path", name), defaultSock);

    return std::make_unique<SimInputChannel>(
        std::string(name),
        std::move(sockPath),
        m_telemetryHost,
        m_telemetryPort,
        m_handlerName);
}

std::unique_ptr<::Backend::IOutputChannel> SimBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig& config)
{
    std::string defaultDest = fmt::format("/tmp/proxy_sim_hold_{}.sock", name);
    std::string sockPath = config.getOr<std::string>(fmt::format("channel.{}.dest_path", name),
        config.getOr<std::string>(fmt::format("channel.{}.path", name), defaultDest));

    return std::make_unique<SimOutputChannel>(
        std::string(name),
        std::move(sockPath),
        m_telemetryHost,
        m_telemetryPort,
        m_handlerName);
}

bool SimBackend::tracksMessageResults() const noexcept
{
    return true;
}

void SimBackend::onMessageResult(
    std::string_view inputChannel,
    std::span<const std::byte> rawMessage,
    const ::Backend::MessageResult& result)
{
    std::span<const std::byte> appPayload = rawMessage;
    std::uint64_t msgTimestampNs = 0;

    if (rawMessage.size() >= 18) {
        std::uint16_t magic = (static_cast<std::uint8_t>(rawMessage[1]) << 8) | static_cast<std::uint8_t>(rawMessage[0]);
        if (magic == 0x5058) {
            std::uint16_t metaLen = static_cast<std::uint8_t>(rawMessage[4])
                | (static_cast<std::uint8_t>(rawMessage[5]) << 8);
            std::uint32_t payloadLen = static_cast<std::uint8_t>(rawMessage[6])
                | (static_cast<std::uint8_t>(rawMessage[7]) << 8)
                | (static_cast<std::uint8_t>(rawMessage[8]) << 16)
                | (static_cast<std::uint8_t>(rawMessage[9]) << 24);
            std::memcpy(&msgTimestampNs, rawMessage.data() + 10, sizeof(std::uint64_t));

            std::size_t offset = 18 + metaLen;
            if (offset <= rawMessage.size()) {
                std::size_t avail = std::min(static_cast<std::size_t>(payloadLen), rawMessage.size() - offset);
                appPayload = rawMessage.subspan(offset, avail);
            }
        }
    }

    std::string_view payloadView(reinterpret_cast<const char*>(appPayload.data()), appPayload.size());
    std::string summary = escapeJsonString(payloadView, 64);
    std::string fullText = escapeJsonString(payloadView, 2048);

    std::string_view resultType = "Consumed";
    std::string_view status = "consumed";
    std::string_view outputChannel;
    std::string_view reason;
    int errorCode = 0;

    std::visit(
        [&](const auto& outcome) {
            using T = std::decay_t<decltype(outcome)>;
            if constexpr (std::is_same_v<T, ::Backend::Forwarded>) {
                resultType = "Forwarded";
                status = "forwarded";
                outputChannel = outcome.channel;
            } else if constexpr (std::is_same_v<T, ::Backend::Consumed>) {
                resultType = "Consumed";
                status = "consumed";
            } else if constexpr (std::is_same_v<T, ::Backend::Dropped>) {
                resultType = "Dropped";
                status = "dropped";
                reason = outcome.reason;
                errorCode = outcome.errorCode;
            }
        },
        result);

    auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                     .count();

    std::string json = fmt::format(
        R"({{"timestampNs":{},"msgTimestampNs":{},"type":"message_result","handler":"{}","inputChannel":"{}","outputChannel":"{}","result":"{}","status":"{}","reason":"{}","errorCode":{},"bytes":{},"summary":"{}","payload":"{}"}})",
        nowNs,
        msgTimestampNs,
        escapeJsonString(m_handlerName, 128),
        escapeJsonString(inputChannel, 128),
        escapeJsonString(outputChannel, 256),
        resultType,
        status,
        escapeJsonString(reason, 512),
        errorCode,
        appPayload.size(),
        summary,
        fullText);

    if (m_unixEventFd >= 0 && !m_eventSockPath.empty()) {
        sockaddr_un addr { };
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, m_eventSockPath.c_str(), sizeof(addr.sun_path) - 1);
        ::sendto(m_unixEventFd, json.data(), json.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }

    if (m_udpEventFd >= 0 && !m_telemetryHost.empty() && m_telemetryPort > 0) {
        sockaddr_in dest { };
        dest.sin_family = AF_INET;
        dest.sin_port = htons(static_cast<uint16_t>(m_telemetryPort));
        ::inet_pton(AF_INET, m_telemetryHost.c_str(), &dest.sin_addr);
        ::sendto(m_udpEventFd, json.data(), json.size(), 0, reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    }
}

void SimBackend::onHandlerStarted()
{
    if (m_unixEventFd >= 0 && !m_eventSockPath.empty()) {
        std::string json = fmt::format(
            R"({{"type":"handler_ready","handler":"{}"}})",
            escapeJsonString(m_handlerName, 128));
        sockaddr_un addr { };
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, m_eventSockPath.c_str(), sizeof(addr.sun_path) - 1);
        ::sendto(m_unixEventFd, json.data(), json.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }
}

} // namespace Proxy::Backend::Sim

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Sim::SimBackend>();
}

} // namespace Backend
