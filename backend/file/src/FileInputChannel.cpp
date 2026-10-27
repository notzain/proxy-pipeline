#include "backend/file/FileInputChannel.hpp"
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
    Proxy::Log::info("[FileInputChannel] Watching directory: {}", m_watchDir.string());
}

BytesResult FileInputChannel::receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout)
{
    if (destination.empty()) {
        return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)));
    }

    const auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::error_code ec;
        std::filesystem::directory_iterator entry(m_watchDir, ec);
        if (ec) {
            return BytesResult::failure(::Backend::receiveFailure(ec));
        }
        const std::filesystem::directory_iterator end;
        for (; entry != end; entry.increment(ec)) {
            if (ec) {
                return BytesResult::failure(::Backend::receiveFailure(ec));
            }
            if (!entry->is_regular_file(ec)) {
                if (ec) {
                    return BytesResult::failure(::Backend::receiveFailure(ec));
                }
                continue;
            }

            auto filePath = entry->path();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::io_error)));
            }
            const auto fileSize = file.tellg();
            if (fileSize < 0) {
                return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::io_error)));
            }
            const auto bytesToRead = std::min(destination.size(), static_cast<std::size_t>(fileSize));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(bytesToRead));
            if (!file && bytesToRead != 0) {
                return BytesResult::failure(::Backend::receiveFailure(std::make_error_code(std::errc::io_error)));
            }
            file.close();

            std::error_code rmEc;
            std::filesystem::remove(filePath, rmEc);
            if (rmEc) {
                Proxy::Log::warn("[FileInputChannel] Could not delete processed file {}: {}", filePath.string(), rmEc.message());
            } else {
                Proxy::Log::debug("[FileInputChannel] Read and deleted processed file {}", filePath.string());
            }

            return BytesResult::success(bytesToRead);
        }
        if (ec) {
            return BytesResult::failure(::Backend::receiveFailure(ec));
        }

        const auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return BytesResult::failure(::Backend::receiveTimeout());
}

} // namespace Proxy::Backend::File
