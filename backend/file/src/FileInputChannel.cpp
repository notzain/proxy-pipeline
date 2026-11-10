#include "backend/file/FileInputChannel.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <system_error>
#include <thread>
#include <utility>

namespace Proxy::Backend::File {

FileInputChannel::FileInputChannel(std::string name, std::filesystem::path watchDir)
    : ::Backend::IInputChannel(std::move(name))
    , m_watchDir(std::move(watchDir))
{
    std::error_code ec;
    std::filesystem::create_directories(m_watchDir, ec);
    PROXY_LOG_INFO("Watching directory: '{directory}'", m_watchDir.string());
}

BytesResult FileInputChannel::receiveSlot(::Backend::BufferSlot& slot, Timeout timeout)
{
    slot.size = 0;
    const auto destination = slot.buffer;
    if (destination.empty()) {
        return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::invalid_argument) });
    }

    const auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::error_code ec;
        std::filesystem::directory_iterator entry(m_watchDir, ec);
        if (ec) {
            return BytesResult::err(Common::Net::ReceiveFailure { ec });
        }
        const std::filesystem::directory_iterator end;
        for (; entry != end; entry.increment(ec)) {
            if (ec) {
                return BytesResult::err(Common::Net::ReceiveFailure { ec });
            }
            if (!entry->is_regular_file(ec)) {
                if (ec) {
                    return BytesResult::err(Common::Net::ReceiveFailure { ec });
                }
                continue;
            }

            auto filePath = entry->path();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::io_error) });
            }
            const auto fileSize = file.tellg();
            if (fileSize < 0) {
                return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::io_error) });
            }
            const auto maxBytes = std::min(destination.size(), ::Backend::MaxEnvelopeFrameBytes);
            if (fileSize > static_cast<std::streamoff>(maxBytes)) {
                file.close();
                std::error_code removeError;
                std::filesystem::remove(filePath, removeError);
                if (removeError) {
                    PROXY_LOG_ERROR("Could not remove oversized input file '{filePath}': {errorMessage}",
                        filePath.string(), removeError.message());
                }
                return BytesResult::err(Common::Net::ReceiveFailure {
                    std::make_error_code(std::errc::message_size) });
            }

            const auto bytesToRead = static_cast<std::size_t>(fileSize);
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(bytesToRead));
            if (!file && bytesToRead != 0) {
                return BytesResult::err(Common::Net::ReceiveFailure { std::make_error_code(std::errc::io_error) });
            }
            file.close();

            std::error_code rmEc;
            std::filesystem::remove(filePath, rmEc);
            if (rmEc) {
                PROXY_LOG_WARN("Could not delete processed file '{filePath}': {errorMessage}", filePath.string(), rmEc.message());
            } else {
                PROXY_LOG_DEBUG("Read and deleted processed file '{filePath}'", filePath.string());
            }

            slot.size = bytesToRead;
            return BytesResult::ok(bytesToRead);
        }
        if (ec) {
            return BytesResult::err(Common::Net::ReceiveFailure { ec });
        }

        const auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return BytesResult::err(Common::Net::ReceiveTimeout { });
}

} // namespace Proxy::Backend::File
