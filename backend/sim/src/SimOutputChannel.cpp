#include "SimOutputChannel.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fmt/core.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Proxy::Backend::Sim {

SimOutputChannel::SimOutputChannel(
    std::string channelName,
    std::string destSocketPath,
    std::string eventHost,
    int eventPort,
    std::string handlerName)
    : m_channelName(std::move(channelName))
    , m_destSocketPath(std::move(destSocketPath))
    , m_eventHost(std::move(eventHost))
    , m_eventPort(eventPort)
    , m_handlerName(std::move(handlerName))
{
    m_clientFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    m_eventFd = ::socket(AF_INET, SOCK_DGRAM, 0);
}

SimOutputChannel::~SimOutputChannel()
{
    if (m_clientFd >= 0) {
        ::close(m_clientFd);
    }
    if (m_eventFd >= 0) {
        ::close(m_eventFd);
    }
}

void SimOutputChannel::emitEvent(std::span<const std::byte> message, std::string_view status)
{
    if (m_eventFd < 0 || m_eventHost.empty() || m_eventPort <= 0) {
        return;
    }

    std::string text;
    std::string fullText;
    std::span<const std::byte> appPayload = message;

    if (message.size() >= 18) {
        uint16_t magic = (static_cast<uint8_t>(message[1]) << 8) | static_cast<uint8_t>(message[0]);
        if (magic == 0x5058) {
            uint16_t metaLen = static_cast<uint8_t>(message[4]) | (static_cast<uint8_t>(message[5]) << 8);
            uint32_t payloadLen = static_cast<uint8_t>(message[6])
                | (static_cast<uint8_t>(message[7]) << 8)
                | (static_cast<uint8_t>(message[8]) << 16)
                | (static_cast<uint8_t>(message[9]) << 24);

            std::size_t offset = 18 + metaLen;
            if (offset <= message.size()) {
                std::size_t avail = std::min(static_cast<std::size_t>(payloadLen), message.size() - offset);
                appPayload = message.subspan(offset, avail);
            }
        }
    }

    std::size_t previewLen = std::min(appPayload.size(), std::size_t(64));
    text.reserve(previewLen);
    for (std::size_t i = 0; i < previewLen; ++i) {
        char c = static_cast<char>(appPayload[i]);
        if (c == '"' || c == '\\') {
            text += '\\';
        }
        text += (c >= 32 && c <= 126) ? c : '.';
    }

    std::size_t maxFull = std::min(appPayload.size(), std::size_t(2048));
    fullText.reserve(maxFull);
    for (std::size_t i = 0; i < maxFull; ++i) {
        char c = static_cast<char>(appPayload[i]);
        if (c == '"' || c == '\\') {
            fullText += '\\';
        } else if (c == '\n') {
            fullText += "\\n";
            continue;
        } else if (c == '\r') {
            fullText += "\\r";
            continue;
        } else if (c == '\t') {
            fullText += "\\t";
            continue;
        }
        fullText += (c >= 32 && c <= 126) ? c : '.';
    }

    auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                     .count();

    std::string json = fmt::format(
        R"({{"timestampNs":{},"type":"egress","source":"{}","target":"{}","channel":"{}","status":"{}","bytes":{},"summary":"{}","payload":"{}"}})",
        nowNs, m_handlerName, m_channelName, m_channelName, status, appPayload.size(), text, fullText);

    sockaddr_in dest { };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(m_eventPort));
    ::inet_pton(AF_INET, m_eventHost.c_str(), &dest.sin_addr);

    ::sendto(m_eventFd, json.data(), json.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

bool SimOutputChannel::send(std::span<const std::byte> message)
{
    if (m_clientFd < 0 || m_destSocketPath.empty()) {
        return false;
    }

    sockaddr_un addr { };
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_destSocketPath.c_str(), sizeof(addr.sun_path) - 1);

    ssize_t sent = ::sendto(
        m_clientFd,
        message.data(),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr));

    bool ok = (sent == static_cast<ssize_t>(message.size()));
    emitEvent(message, ok ? "sent" : "send_failed");
    return ok;
}

std::size_t SimOutputChannel::sendBatch(std::span<const std::span<const std::byte>> messages)
{
    std::size_t sentCount = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            sentCount++;
        }
    }
    return sentCount;
}

} // namespace Proxy::Backend::Sim
