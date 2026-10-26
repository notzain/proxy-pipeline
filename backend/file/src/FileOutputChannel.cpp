#include "backend/file/FileOutputChannel.hpp"
#include "log/Log.hpp"
#include <chrono>
#include <fmt/core.h>
#include <fstream>
#include <random>
#include <string>

namespace Proxy::Backend::File {

FileOutputChannel::FileOutputChannel(std::string name, std::filesystem::path outputDir)
    : ::Backend::IOutputChannel(std::move(name))
    , m_outputDir(std::move(outputDir))
{
    std::error_code ec;
    std::filesystem::create_directories(m_outputDir, ec);
    Proxy::Log::info("[FileOutputChannel] Output directory: {}", m_outputDir.string());
}

bool FileOutputChannel::send(std::span<const std::byte> message)
{
    std::error_code ec;
    std::filesystem::create_directories(m_outputDir, ec);

    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                           .count();

    thread_local std::mt19937_64 rng(std::random_device { }());
    const auto randomSuffix = rng();

    std::string fileName = fmt::format("msg_{:016x}_{:016x}.dat", nowNs, randomSuffix);
    auto filePath = m_outputDir / fileName;

    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) {
        Proxy::Log::error("[FileOutputChannel] Failed to write message file {}", filePath.string());
        return false;
    }

    out.write(reinterpret_cast<const char*>(message.data()), static_cast<std::streamsize>(message.size()));
    out.close();

    Proxy::Log::debug("[FileOutputChannel] Successfully wrote {} bytes to {}", message.size(), filePath.string());
    return true;
}

} // namespace Proxy::Backend::File
