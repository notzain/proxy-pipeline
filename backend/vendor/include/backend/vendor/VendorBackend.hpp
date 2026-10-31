#pragma once

#include "backend/IBackend.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <cstddef>
#include <memory>

namespace Backend::Vendor {

/**
 * @brief 3rd-party vendor transport backend implementation.
 *
 * Manages the shared vendor SDK manager and creates vendor input/output channels.
 *
 * Why VendorBackend uses a BufferPool even though `VendorInputChannel::providesOwnMemory()` is true:
 * - `providesOwnMemory() == true` tells execution strategies not to allocate caller scratch buffers, because
 *   `VendorInputChannel` always returns a leased zero-copy span bound to a `VendorLeaseState`.
 * - However, the vendor SDK delivers messages in two ways:
 *   1. Mode A (in-memory `{ byte*, length }`): the SDK already owns the bytes in RAM, so no pool is used.
 *   2. Mode B (file path on disk): the SDK only gives a filesystem path, not memory. `VendorInputChannel`
 *      must load the file into memory owned by `VendorLeaseState` so handlers receive a contiguous byte span,
 *      acquiring a reusable `BufferSlice` from `IBackend::bufferPool()` (or `MappedFile` for oversized files).
 */
class VendorBackend : public ::Backend::IBackend {
public:
    static constexpr std::size_t DefaultPoolBlockSize = 65536;
    static constexpr std::size_t DefaultPoolBlockCount = 256;

    VendorBackend();
    explicit VendorBackend(
        std::shared_ptr<Sdk::VendorManager> manager,
        std::size_t poolBlockSize = DefaultPoolBlockSize,
        std::size_t poolBlockCount = DefaultPoolBlockCount);
    ~VendorBackend() override;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    [[nodiscard]] std::shared_ptr<Sdk::VendorManager> vendorManager() const noexcept;

private:
    std::shared_ptr<Sdk::VendorManager> m_vendorManager;
};

} // namespace Backend::Vendor
