#include "SimInputChannel.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fmt/core.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace Proxy::Backend::Sim {

SimInputChannel::SimInputChannel(
    std::string channelName,
    std::string socketPath,
    std::string eventHost,
    int eventPort,
    std::string handlerName)
    : ::Backend::IInputChannel(channelName)
    , m_channelName(std::move(channelName))
    , m_socketPath(std::move(socketPath))
    , m_eventHost(std::move(eventHost))
    , m_eventPort(eventPort)
    , m_handlerName(std::move(handlerName))
{
    m_eventFd = ::socket(AF_INET, SOCK_DGRAM, 0);

    m_serverFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_serverFd >= 0) {
        ::unlink(m_socketPath.c_str());
        sockaddr_un addr { };
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);
        if (::bind(m_serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            Proxy::Log::warn("[SimInputChannel] Failed to bind IPC socket '{}': {}", m_socketPath, strerror(errno));
            ::close(m_serverFd);
            m_serverFd = -1;
        } else {
            int flags = ::fcntl(m_serverFd, F_GETFL, 0);
            ::fcntl(m_serverFd, F_SETFL, flags | O_NONBLOCK);
            int rcvbuf = 4 * 1024 * 1024;
            ::setsockopt(m_serverFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
        }
    }
}

SimInputChannel::~SimInputChannel()
{
    if (m_serverFd >= 0) {
        ::close(m_serverFd);
        ::unlink(m_socketPath.c_str());
    }
    if (m_eventFd >= 0) {
        ::close(m_eventFd);
    }
}

void SimInputChannel::emitEvent(std::span<const std::byte> payload, std::string_view status)
{
    if (m_eventFd < 0 || m_eventHost.empty() || m_eventPort <= 0) {
        return;
    }

    // Inspect if envelope is present (18-byte EnvelopeHeader starting with 'PX' = 0x5058)
    std::string text;
    std::string fullText;
    std::span<const std::byte> appPayload = payload;

    if (payload.size() >= 18) {
        uint16_t magic = (static_cast<uint8_t>(payload[1]) << 8) | static_cast<uint8_t>(payload[0]);
        if (magic == 0x5058) {
            uint16_t metaLen = static_cast<uint8_t>(payload[4]) | (static_cast<uint8_t>(payload[5]) << 8);
            uint32_t payloadLen = static_cast<uint8_t>(payload[6])
                | (static_cast<uint8_t>(payload[7]) << 8)
                | (static_cast<uint8_t>(payload[8]) << 16)
                | (static_cast<uint8_t>(payload[9]) << 24);

            std::size_t offset = 18 + metaLen;
            if (offset <= payload.size()) {
                std::size_t avail = std::min(static_cast<std::size_t>(payloadLen), payload.size() - offset);
                appPayload = payload.subspan(offset, avail);
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
        R"({{"timestampNs":{},"type":"hop","source":"{}","target":"{}","channel":"{}","status":"{}","bytes":{},"summary":"{}","payload":"{}"}})",
        nowNs, m_channelName, m_handlerName, m_channelName, status, appPayload.size(), text, fullText);

    sockaddr_in dest { };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(m_eventPort));
    ::inet_pton(AF_INET, m_eventHost.c_str(), &dest.sin_addr);

    ::sendto(m_eventFd, json.data(), json.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

BytesResult SimInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    std::array<::Backend::BufferSlot, 1> slots { ::Backend::BufferSlot { destination, 0 } };
    BatchResult result = receiveBatch(slots, timeout);
    if (!result.hasValue()) {
        return BytesResult::failure(result.error().cause);
    }
    return BytesResult::success(slots[0].size);
}

BatchResult SimInputChannel::receiveBatch(
    std::span<::Backend::BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty() || m_serverFd < 0) {
        return slots.empty()
            ? BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) })
            : BatchResult::failure({ 0, ::Backend::receiveClosed() });
    }

    std::size_t count = 0;
    auto start = std::chrono::steady_clock::now();

    while (count < slots.size()) {
        ssize_t n = ::recvfrom(
            m_serverFd,
            slots[count].buffer.data(),
            slots[count].buffer.size(),
            0,
            nullptr,
            nullptr);

        if (n >= 0) {
            slots[count].size = static_cast<std::size_t>(n);
            emitEvent(slots[count].filledSpan(), "channel_received");
            count++;
            continue;
        }

        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno == EBADF || errno == ENOTSOCK) {
                return BatchResult::failure({ count, ::Backend::receiveClosed() });
            }
            return BatchResult::failure({ count, ::Backend::receiveFailure(std::error_code(errno, std::generic_category())) });
        }

        if (count > 0 || timeout.count() <= 0) {
            break;
        }

        auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - start);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    return count > 0
        ? BatchResult::success(count)
        : BatchResult::failure({ 0, ::Backend::receiveTimeout() });
}

} // namespace Proxy::Backend::Sim
