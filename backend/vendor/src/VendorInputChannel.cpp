#include "backend/vendor/VendorInputChannel.hpp"
#include "VendorLeaseState.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "common/memory/DynamicBuffer.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace Backend::Vendor {

namespace {

    /**
     * @brief Loads a vendor payload file into a pooled BufferSlice, or maps it privately when it does not fit.
     *
     * Small files are read into a fixed-size `BufferPool` block. Files larger than a block (or arriving while
     * the pool is exhausted) are memory-mapped instead of copied to the heap, so memory usage stays bounded
     * by the pool plus the OS page cache rather than growing unbounded heap allocations.
     */
    bool loadPayloadFile(
        const std::string& filePath,
        const std::shared_ptr<Common::Memory::BufferPool>& bufferPool,
        Common::Memory::DynamicBuffer& outBuffer)
    {
        std::error_code ec;
        const auto fileSize = static_cast<std::size_t>(std::filesystem::file_size(filePath, ec));
        if (ec) {
            return false;
        }
        if (fileSize == 0) {
            return true;
        }

        if (bufferPool && fileSize <= bufferPool->blockSize()) {
            if (auto slice = bufferPool->acquire()) {
                std::ifstream file(filePath, std::ios::binary);
                if (!file.read(reinterpret_cast<char*>(slice->data()), static_cast<std::streamsize>(fileSize))) {
                    return false;
                }
                slice->resize(fileSize);
                outBuffer = std::move(*slice);
                return true;
            }
            Proxy::Log::debug("[VendorInputChannel] BufferPool exhausted, mapping payload file '{}'", filePath);
        }

        auto mapping = Common::Io::MappedFile::open(filePath);
        if (!mapping) {
            return false;
        }
        outBuffer = std::move(*mapping);
        return true;
    }

} // namespace

VendorInputChannel::VendorInputChannel(
    std::string name,
    std::shared_ptr<Sdk::VendorManager> manager,
    std::shared_ptr<Sdk::VendorReceiver> receiver,
    std::shared_ptr<Common::Memory::BufferPool> bufferPool)
    : ::Backend::IInputChannel(std::move(name))
    , m_manager(std::move(manager))
    , m_receiver(std::move(receiver))
    , m_bufferPool(std::move(bufferPool))
{
}

VendorInputChannel::VendorInputChannel(
    std::string name,
    VendorBackend& backend,
    std::shared_ptr<Sdk::VendorReceiver> receiver)
    : VendorInputChannel(std::move(name), backend.vendorManager(), std::move(receiver), backend.bufferPool())
{
}

void VendorInputChannel::setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver)
{
    m_receiver = std::move(receiver);
}

bool VendorInputChannel::providesOwnMemory() const noexcept
{
    return true;
}

::Backend::BytesResult VendorInputChannel::receiveSlot(
    ::Backend::BufferSlot& slot,
    Timeout timeout)
{
    slot.reset();

    if (!m_receiver) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveClosed { });
    }

    if (!m_manager) {
        Proxy::Log::error("[VendorInputChannel] VendorManager not initialized");
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure { });
    }

    Sdk::VendorMessage rawMsg;
    if (!m_receiver->receive(&rawMsg, static_cast<int>(timeout.count()))) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveTimeout { });
    }

    Common::Memory::DynamicBuffer fileBuffer;

    const std::string& filePath = rawMsg.getPayloadFilePath();
    if (!filePath.empty()) {
        if (!loadPayloadFile(filePath, m_bufferPool, fileBuffer)) {
            Proxy::Log::error("[VendorInputChannel] Failed to read payload file '{}' for message ID {}",
                filePath, rawMsg.getId());
            m_manager->drop(rawMsg);
            return ::Backend::BytesResult::failure(::Backend::ReceiveFailure { });
        }
    }

    auto leaseState = std::make_unique<VendorLeaseState>(
        std::move(rawMsg),
        m_manager,
        std::move(fileBuffer));

    const auto view = leaseState->payloadView();
    slot.assignLeased(view, ::Backend::Lease(std::move(leaseState)));
    return ::Backend::BytesResult::success(slot.size);
}

} // namespace Backend::Vendor
