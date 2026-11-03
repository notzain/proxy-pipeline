#include "backend/sim/SimBackend.hpp"
#include "SimInputChannel.hpp"
#include "SimOutputChannel.hpp"
#include "backend/message/MessageView.hpp"
#include "log/ConsoleLogSink.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

/**
 * @brief Splits a string_view by a delimiter character and trims whitespace.
 */
std::vector<std::string> splitChannelList(std::string_view str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream { std::string(str) };
    while (std::getline(tokenStream, token, delimiter)) {
        auto start = token.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            auto end = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}

std::string sanitizePayload(std::string_view input, std::size_t maxLen)
{
    std::size_t limit = std::min(input.size(), maxLen);
    std::string out;
    out.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        char c = input[i];
        out += (c >= 32 && c <= 126) ? c : '.';
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
    Common::Compat::span<const std::byte> rawMessage,
    const ::Backend::MessageResult& result)
{
    Common::Compat::span<const std::byte> appPayload = rawMessage;
    std::uint64_t msgTimestampNs = 0;

    if (auto optView = ::Backend::MessageView::from(rawMessage)) {
        appPayload = optView->payload();
        msgTimestampNs = optView->timestampNs();
    }

    std::string_view payloadView(reinterpret_cast<const char*>(appPayload.data()), appPayload.size());
    std::string summary = sanitizePayload(payloadView, 64);
    std::string fullText = sanitizePayload(payloadView, 2048);

    auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                     .count();

    nlohmann::json root;
    root["timestampNs"] = nowNs;
    root["msgTimestampNs"] = msgTimestampNs;
    root["type"] = "message_result";
    root["handler"] = m_handlerName;
    root["inputChannel"] = inputChannel;
    root["bytes"] = appPayload.size();

    nlohmann::json traces = nlohmann::json::array();

    result.match(
        [&](const ::Backend::Forwarded& outcome) {
            auto outChannels = splitChannelList(outcome.channel, ',');
            if (outChannels.empty()) {
                nlohmann::json tr;
                tr["traceId"] = 1;
                tr["outputChannel"] = "";
                tr["result"] = "Forwarded";
                tr["status"] = "forwarded";
                tr["summary"] = summary;
                tr["payload"] = fullText;
                traces.push_back(std::move(tr));
            } else {
                for (std::size_t i = 0; i < outChannels.size(); ++i) {
                    nlohmann::json tr;
                    tr["traceId"] = i + 1;
                    tr["outputChannel"] = outChannels[i];
                    tr["result"] = "Forwarded";
                    tr["status"] = "forwarded";
                    tr["summary"] = summary;
                    tr["payload"] = fullText;
                    traces.push_back(std::move(tr));
                }
            }
        },
        [&](const ::Backend::Consumed&) {
            nlohmann::json tr;
            tr["traceId"] = 1;
            tr["result"] = "Consumed";
            tr["status"] = "consumed";
            tr["summary"] = summary;
            tr["payload"] = fullText;
            traces.push_back(std::move(tr));
        },
        [&](const ::Backend::Dropped& outcome) {
            nlohmann::json tr;
            tr["traceId"] = 1;
            tr["result"] = "Dropped";
            tr["status"] = "dropped";
            tr["reason"] = std::string(outcome.reason);
            tr["errorCode"] = outcome.errorCode;
            tr["summary"] = summary;
            tr["payload"] = fullText;
            traces.push_back(std::move(tr));
        });

    root["traces"] = std::move(traces);
    std::string json = root.dump();

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
        nlohmann::json readyObj;
        readyObj["type"] = "handler_ready";
        readyObj["handler"] = m_handlerName;
        std::string json = readyObj.dump();

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
