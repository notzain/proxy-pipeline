#include "backend/file/FileInputChannel.hpp"
#include "log/Log.hpp"
#include <chrono>
#include <fstream>
#include <thread>
#include <utility>

namespace Proxy::Backend::File {

FileInputChannel::FileInputChannel(std::filesystem::path watchDir)
    : m_watchDir(std::move(watchDir))
{
    std::error_code ec;
    std::filesystem::create_directories(m_watchDir, ec);
    Proxy::Log::info("[FileInputChannel] Watching directory: {}", m_watchDir.string());
}

std::optional<::Backend::IInputChannel::Buffer> FileInputChannel::receive(Timeout timeout)
{
    const auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::error_code ec;
        if (std::filesystem::exists(m_watchDir, ec) && std::filesystem::is_directory(m_watchDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_watchDir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }

                auto filePath = entry.path();
                std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    const auto fileSize = file.tellg();
                    if (fileSize >= 0) {
                        Buffer buffer(static_cast<std::size_t>(fileSize));
                        file.seekg(0, std::ios::beg);
                        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
                        file.close();

                        // Delete the processed file so no tracking is needed
                        std::error_code rmEc;
                        std::filesystem::remove(filePath, rmEc);
                        if (rmEc) {
                            Proxy::Log::warn("[FileInputChannel] Could not delete processed file {}: {}", filePath.string(), rmEc.message());
                        } else {
                            Proxy::Log::debug("[FileInputChannel] Read and deleted processed file {}", filePath.string());
                        }

                        return buffer;
                    }
                }
            }
        }

        const auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return std::nullopt;
}

std::size_t FileInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    const auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::error_code ec;
        if (std::filesystem::exists(m_watchDir, ec) && std::filesystem::is_directory(m_watchDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_watchDir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }

                auto filePath = entry.path();
                std::ifstream file(filePath, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    const auto fileSize = file.tellg();
                    if (fileSize >= 0) {
                        const auto bytesToRead = std::min(destination.size(), static_cast<std::size_t>(fileSize));
                        file.seekg(0, std::ios::beg);
                        file.read(reinterpret_cast<char*>(destination.data()), bytesToRead);
                        file.close();

                        // Delete the processed file so no tracking is needed
                        std::error_code rmEc;
                        std::filesystem::remove(filePath, rmEc);
                        if (rmEc) {
                            Proxy::Log::warn("[FileInputChannel] Could not delete processed file {}: {}", filePath.string(), rmEc.message());
                        } else {
                            Proxy::Log::debug("[FileInputChannel] Read and deleted processed file {}", filePath.string());
                        }

                        return bytesToRead;
                    }
                }
            }
        }

        const auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return 0;
}

} // namespace Proxy::Backend::File
