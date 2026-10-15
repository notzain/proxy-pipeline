#include "backend/file/FileBackend.hpp"
#include <chrono>
#include <fstream>
#include <spdlog/spdlog.h>
#include <thread>
#include <utility>

namespace Proxy::Backend::File {

void FileBackend::configure(const Core::Configuration& config)
{
    if (auto path = config.get("file.path"); path.has_value()) {
        m_watchDirectory = *path;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_watchDirectory, ec);
    spdlog::info("[FileBackend] Watching directory: {}", m_watchDirectory.string());
}

std::optional<Core::IBackend::Buffer> FileBackend::nextMessage(Timeout timeout)
{
    const auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::error_code ec;
        if (std::filesystem::exists(m_watchDirectory, ec) && std::filesystem::is_directory(m_watchDirectory, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_watchDirectory, ec)) {
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
                            spdlog::warn("[FileBackend] Could not delete processed file {}: {}", filePath.string(), rmEc.message());
                        } else {
                            spdlog::debug("[FileBackend] Read and deleted processed file {}", filePath.string());
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

} // namespace Proxy::Backend::File

namespace Core {

std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::File::FileBackend>();
}

} // namespace Core
