#include "backend/uds/UdsInputChannel.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "common/net/ISocketPoller.hpp"
#include "log/Log.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Uds {

UdsInputChannel::UdsInputChannel(std::string name, std::string socketPath)
    : ::Backend::IInputChannel(std::move(name))
    , m_socketPath(std::move(socketPath))
    , m_poller(::Common::Net::createSocketPoller())
{
    openAndBindSocket();
}

UdsInputChannel::~UdsInputChannel()
{
    closeSocket();
}

void UdsInputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
    if (!m_socketPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_socketPath, ec);
    }
}

void UdsInputChannel::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        PROXY_LOG_ERROR("Failed to create UDS socket: {errorMessage}", std::strerror(errno));
        return;
    }

    int rcvbuf = 8 * 1024 * 1024; // 8MB receive buffer for high-throughput bursts
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    if (m_socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
        PROXY_LOG_ERROR("Socket path too long: '{socketPath}'", m_socketPath);
        closeSocket();
        return;
    }

    // Ensure parent directory exists
    std::filesystem::path p(m_socketPath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // Unlink old socket file if it exists
    ::unlink(m_socketPath.c_str());

    sockaddr_un addr { };
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        PROXY_LOG_ERROR("Failed to bind UDS socket to '{socketPath}': {errorMessage}", m_socketPath, std::strerror(errno));
        closeSocket();
        return;
    }

    PROXY_LOG_INFO("Listening on UDS path '{socketPath}'", m_socketPath);
}

BytesResult UdsInputChannel::receiveSlot(::Backend::BufferSlot& slot, Timeout timeout)
{
    slot.size = 0;
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::bad_file_descriptor) });
        }
    }

    if (m_poller) {
        auto receiveBuffer = slot.buffer.first(std::min(slot.buffer.size(), ::Backend::MaxEnvelopeFrameBytes));
        auto result = m_poller->receive(m_socketFd, receiveBuffer, timeout);
        if (result.isOk()) {
            slot.size = result.unwrap();
        }
        return result;
    }

    return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::not_connected) });
}

BatchResult UdsInputChannel::receiveBatch(
    Common::Compat::span<::Backend::BufferSlot> slots,
    Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return BatchResult::err(Common::Net::ReceiveBatchFailure { 0, Common::Net::ReceiveFailure { std::make_error_code(std::errc::bad_file_descriptor) } });
        }
    }

    if (slots.empty()) {
        return BatchResult::err(Common::Net::ReceiveBatchFailure { 0, Common::Net::ReceiveFailure { std::make_error_code(std::errc::invalid_argument) } });
    }

    if (m_poller) {
        constexpr std::size_t MaxBatch = 64;
        const std::size_t batchSize = std::min(slots.size(), MaxBatch);
        std::array<Common::Net::PollerBufferSlot, MaxBatch> pollerSlots;
        for (std::size_t i = 0; i < batchSize; ++i) {
            slots[i].size = 0;
            const auto receiveBuffer = slots[i].buffer.first(
                std::min(slots[i].buffer.size(), ::Backend::MaxEnvelopeFrameBytes));
            pollerSlots[i] = Common::Net::PollerBufferSlot { receiveBuffer, 0 };
        }

        auto result = m_poller->receiveBatch(
            m_socketFd,
            Common::Compat::span<Common::Net::PollerBufferSlot>(pollerSlots.data(), batchSize),
            timeout);

        const std::size_t receivedCount = result.isOk() ? result.unwrap() : result.unwrapErr().receivedCount;
        for (std::size_t i = 0; i < receivedCount; ++i) {
            slots[i].size = pollerSlots[i].size;
        }

        return result;
    }

    return BatchResult::err(Common::Net::ReceiveBatchFailure { 0, Common::Net::ReceiveFailure { std::make_error_code(std::errc::not_connected) } });
}

} // namespace Proxy::Backend::Uds
