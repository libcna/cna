#pragma once

#include "CNA/Internal/Backends/Skia/SkiaResourceCounters.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Skia
{
    /**
     * Immutable byte layout for one CNA-owned block-compressed 2D mip level. width/height are the
     * exact texel dimensions at this level; blockCols/blockRows are ceil(width/4)/ceil(height/4)
     * padded block counts. rowBytes = blockCols * blockBytes; bytes = rowBytes * blockRows.
     */
    struct SkiaCompressedMipLevel2D final
    {
        int width = 0;
        int height = 0;
        int blockCols = 0;
        int blockRows = 0;
        std::size_t rowBytes = 0;
        std::size_t offset = 0;
        std::size_t bytes = 0;

        [[nodiscard]] bool operator==(const SkiaCompressedMipLevel2D&) const = default;
    };

    enum class SkiaCompressedMipChain2DLayoutError
    {
        None,
        InvalidDimensions,
        InvalidBlockByteSize,
        SizeOverflow,
        BudgetExceeded,
    };

    /**
     * Describes a level-0-only image or complete floor-halved chain of padded block layouts
     * without allocating storage. On failure, levels and storageBytes retain their caller-provided
     * values. Each level's texel dimensions floor-halve exactly like an uncompressed chain; its
     * block counts are independently ceil-padded from those texel dimensions, matching how a real
     * BC1/BC2/BC3 mip chain is authored and stored.
     */
    [[nodiscard]] bool TryBuildSkiaCompressedMipChain2DLayout(
        int width, int height, bool mipMap, std::size_t blockByteSize,
        std::vector<SkiaCompressedMipLevel2D>& levels, std::size_t& storageBytes,
        SkiaCompressedMipChain2DLayoutError& error);

    /**
     * Stable, contiguous, zero-initialized CPU storage for a checked block-compressed 2D mip
     * chain. Descriptors and byte addresses never move during the object's lifetime. Unlike
     * SkiaMipChain2D, levels beyond zero are never deterministically generated: real-time
     * block-compression re-encoding has no direct Skia equivalent, so every mip level must be
     * explicitly authored by the caller.
     */
    class SkiaCompressedMipChain2D final
    {
    public:
        SkiaCompressedMipChain2D(int width, int height, bool mipMap, std::size_t blockByteSize,
                                 std::shared_ptr<SkiaResourceCounters> resourceCounters = {});
        ~SkiaCompressedMipChain2D();

        SkiaCompressedMipChain2D(const SkiaCompressedMipChain2D&) = delete;
        SkiaCompressedMipChain2D& operator=(const SkiaCompressedMipChain2D&) = delete;
        SkiaCompressedMipChain2D(SkiaCompressedMipChain2D&&) = delete;
        SkiaCompressedMipChain2D& operator=(SkiaCompressedMipChain2D&&) = delete;

        [[nodiscard]] int LevelCount() const noexcept
        {
            return static_cast<int>(levels_.size());
        }
        [[nodiscard]] std::size_t StorageBytes() const noexcept { return storage_.size(); }
        [[nodiscard]] const SkiaCompressedMipLevel2D& Level(int level) const;
        [[nodiscard]] std::uint8_t* LevelData(int level);
        [[nodiscard]] const std::uint8_t* LevelData(int level) const;

    private:
        std::vector<SkiaCompressedMipLevel2D> levels_;
        std::vector<std::uint8_t> storage_;
        std::shared_ptr<SkiaResourceCounters> resourceCounters_;
        bool resourceRegistered_ = false;
    };
} // namespace CNA::Internal::Backends::Skia
