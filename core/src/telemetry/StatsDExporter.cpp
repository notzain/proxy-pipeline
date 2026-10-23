#include "core/telemetry/StatsDExporter.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fmt/core.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Core::Telemetry {

StatsDExporter::StatsDExporter(
    std::string host,
    uint16_t port,
    std::string prefix,
    MetricsRegistry& registry)
    : m_host(std::move(host))
    , m_port(port)
    , m_prefix(std::move(prefix))
    , m_registry(registry)
{
    initSocket();
}

StatsDExporter::~StatsDExporter()
{
    stop();
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void StatsDExporter::initSocket()
{
    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[StatsDExporter] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    struct sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    if (::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) <= 0) {
        Proxy::Log::error("[StatsDExporter] Invalid host address '{}'", m_host);
        ::close(m_socketFd);
        m_socketFd = -1;
        return;
    }

    if (::connect(m_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        Proxy::Log::error("[StatsDExporter] Failed to connect UDP socket to {}:{}: {}", m_host, m_port, std::strerror(errno));
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void StatsDExporter::start(std::chrono::milliseconds interval)
{
    if (m_running.exchange(true)) {
        return;
    }

    m_worker = std::thread([this, interval]() {
        while (m_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(interval);
            if (m_running.load(std::memory_order_relaxed)) {
                flushNow();
            }
        }
    });
}

void StatsDExporter::stop()
{
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void StatsDExporter::sendRaw(std::string_view payload)
{
    if (m_socketFd < 0 || payload.empty()) {
        return;
    }
    ::send(m_socketFd, payload.data(), payload.size(), 0);
}

void StatsDExporter::sendCount(std::string_view metric, int64_t count)
{
    std::string msg;
    if (!m_prefix.empty()) {
        msg = fmt::format("{}.{}:{}|c\n", m_prefix, metric, count);
    } else {
        msg = fmt::format("{}:{}|c\n", metric, count);
    }
    sendRaw(msg);
}

void StatsDExporter::sendGauge(std::string_view metric, int64_t value)
{
    std::string msg;
    if (!m_prefix.empty()) {
        msg = fmt::format("{}.{}:{}|g\n", m_prefix, metric, value);
    } else {
        msg = fmt::format("{}:{}|g\n", metric, value);
    }
    sendRaw(msg);
}

void StatsDExporter::sendTiming(std::string_view metric, double milliseconds)
{
    std::string msg;
    if (!m_prefix.empty()) {
        msg = fmt::format("{}.{}:{:.3f}|ms\n", m_prefix, metric, milliseconds);
    } else {
        msg = fmt::format("{}:{:.3f}|ms\n", metric, milliseconds);
    }
    sendRaw(msg);
}

void StatsDExporter::flushNow()
{
    if (m_socketFd < 0) {
        return;
    }

    // Convert Prometheus text representation into StatsD format lines
    // or serialize directly from registry
    std::string prom = m_registry.toPrometheus();
    if (prom.empty()) {
        return;
    }

    // Parse Prometheus format line-by-line and send StatsD metrics
    std::string_view remaining = prom;
    std::string currentType = "g";

    while (!remaining.empty()) {
        auto newlinePos = remaining.find('\n');
        std::string_view line = (newlinePos == std::string_view::npos) ? remaining : remaining.substr(0, newlinePos);
        remaining = (newlinePos == std::string_view::npos) ? std::string_view { } : remaining.substr(newlinePos + 1);

        if (line.empty()) {
            continue;
        }

        if (line.starts_with("# TYPE ")) {
            auto space1 = line.find(' ', 7);
            if (space1 != std::string_view::npos) {
                std::string_view type = line.substr(space1 + 1);
                if (type == "counter") {
                    currentType = "c";
                } else if (type == "gauge") {
                    currentType = "g";
                } else if (type == "histogram") {
                    currentType = "ms";
                } else {
                    currentType = "g";
                }
            }
            continue;
        }

        if (line.starts_with('#')) {
            continue;
        }

        auto spacePos = line.find(' ');
        if (spacePos == std::string_view::npos) {
            continue;
        }

        std::string_view key = line.substr(0, spacePos);
        std::string_view val = line.substr(spacePos + 1);

        // Filter bucket lines or convert:
        if (key.ends_with("_sum")) {
            auto base = key.substr(0, key.size() - 4);
            sendRaw(fmt::format("{}.{}.sum:{}|ms\n", m_prefix, base, val));
        } else if (key.ends_with("_count")) {
            auto base = key.substr(0, key.size() - 6);
            sendRaw(fmt::format("{}.{}.count:{}|c\n", m_prefix, base, val));
        } else if (auto bucketPos = key.find("_bucket{le=\""); bucketPos != std::string_view::npos) {
            auto base = key.substr(0, bucketPos);
            auto leStart = bucketPos + 12;
            auto leEnd = key.find('"', leStart);
            if (leEnd != std::string_view::npos) {
                auto le = key.substr(leStart, leEnd - leStart);
                sendRaw(fmt::format("{}.{}.bucket.{}:{}|g\n", m_prefix, base, le, val));
            }
        } else if (key.find('{') == std::string_view::npos) {
            sendRaw(fmt::format("{}.{}:{}|{}\n", m_prefix, key, val, currentType));
        }
    }
}

} // namespace Core::Telemetry
