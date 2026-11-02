#include "backend/message/InspectionView.hpp"
#include <array>

namespace Backend {

std::optional<InspectionView> InspectionView::from(const MessageView& message) noexcept
{
    const auto meta = message.metadata();
    if (meta.size() < sizeof(InspectionHeader)) {
        return std::nullopt;
    }

    InspectionHeader header;
    std::memcpy(&header, meta.data(), sizeof(InspectionHeader));

    if (header.magic != PipelineInspectionMagic || header.totalSplits == 0 || header.splitIndex >= header.totalSplits) {
        return std::nullopt;
    }

    return InspectionView(message, header);
}

std::vector<std::byte> InspectionView::pack(const MessageView& message, const InspectionHeader& header)
{
    const auto origMeta = message.metadata();
    const std::size_t combinedMetaSize = sizeof(InspectionHeader) + origMeta.size();

    constexpr std::size_t MaxStackMetaSize = 512;
    std::array<std::byte, MaxStackMetaSize> stackBuf;
    std::vector<std::byte> heapBuf;
    std::byte* metaPtr = nullptr;

    if (combinedMetaSize <= MaxStackMetaSize) {
        metaPtr = stackBuf.data();
    } else {
        heapBuf.resize(combinedMetaSize);
        metaPtr = heapBuf.data();
    }

    InspectionHeader finalHeader = header;
    finalHeader.magic = PipelineInspectionMagic;
    finalHeader.originalMetadataLength = static_cast<std::uint16_t>(origMeta.size());

    std::memcpy(metaPtr, &finalHeader, sizeof(finalHeader));
    if (!origMeta.empty()) {
        std::memcpy(metaPtr + sizeof(InspectionHeader), origMeta.data(), origMeta.size());
    }

    return MessageView::pack(
        message.protocol(),
        Common::Compat::span<const std::byte>(metaPtr, combinedMetaSize),
        message.payload(),
        message.timestampNs());
}

Common::Compat::span<const std::byte> InspectionView::extractMetadata() const noexcept
{
    const auto meta = m_message.metadata();
    if (meta.size() <= sizeof(InspectionHeader)) {
        return {};
    }
    const auto available = meta.size() - sizeof(InspectionHeader);
    const auto length = std::min(static_cast<std::size_t>(m_header.originalMetadataLength), available);
    return meta.subspan(sizeof(InspectionHeader), length);
}

} // namespace Backend

