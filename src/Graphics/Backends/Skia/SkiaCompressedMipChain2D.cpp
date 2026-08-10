#include "CNA/Internal/Backends/Skia/SkiaCompressedMipChain2D.hpp"

#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] int BlockCount(int texels) noexcept
        {
            return (texels + 3) / 4;
        }
    }

    bool TryBuildSkiaCompressedMipChain2DLayout(
        int width, int height, bool mipMap, std::size_t blockByteSize,
        std::vector<SkiaCompressedMipLevel2D>& levels, std::size_t& storageBytes,
        SkiaCompressedMipChain2DLayoutError& error)
    {
        if (!IsValidSkiaResourceAxis(width) || !IsValidSkiaResourceAxis(height))
        {
            error = SkiaCompressedMipChain2DLayoutError::InvalidDimensions;
            return false;
        }
        if (blockByteSize == 0u)
        {
            error = SkiaCompressedMipChain2DLayoutError::InvalidBlockByteSize;
            return false;
        }

        std::vector<SkiaCompressedMipLevel2D> builtLevels;
        int levelWidth = width;
        int levelHeight = height;
        std::size_t total = 0u;
        do
        {
            const int blockCols = BlockCount(levelWidth);
            const int blockRows = BlockCount(levelHeight);
            std::size_t rowBytes = 0u;
            std::size_t levelBytes = 0u;
            if (!CheckedSizeMultiply(static_cast<std::size_t>(blockCols), blockByteSize, rowBytes)
                || !CheckedSizeMultiply(rowBytes, static_cast<std::size_t>(blockRows), levelBytes))
            {
                error = SkiaCompressedMipChain2DLayoutError::SizeOverflow;
                return false;
            }

            std::size_t nextTotal = 0u;
            if (!CheckedSizeAdd(total, levelBytes, nextTotal))
            {
                error = SkiaCompressedMipChain2DLayoutError::SizeOverflow;
                return false;
            }
            if (nextTotal > kSkiaCpuTextureStorageLimitBytes)
            {
                error = SkiaCompressedMipChain2DLayoutError::BudgetExceeded;
                return false;
            }

            builtLevels.push_back(
                {levelWidth, levelHeight, blockCols, blockRows, rowBytes, total, levelBytes});
            total = nextTotal;
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }
        while (mipMap
               && (builtLevels.back().width > 1 || builtLevels.back().height > 1));

        levels = std::move(builtLevels);
        storageBytes = total;
        error = SkiaCompressedMipChain2DLayoutError::None;
        return true;
    }

    SkiaCompressedMipChain2D::SkiaCompressedMipChain2D(
        int width, int height, bool mipMap, std::size_t blockByteSize,
        std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : resourceCounters_(std::move(resourceCounters))
    {
        std::size_t storageBytes = 0u;
        SkiaCompressedMipChain2DLayoutError error = SkiaCompressedMipChain2DLayoutError::None;
        if (!TryBuildSkiaCompressedMipChain2DLayout(width, height, mipMap, blockByteSize,
                                                     levels_, storageBytes, error))
        {
            if (error == SkiaCompressedMipChain2DLayoutError::InvalidDimensions)
                throw std::invalid_argument("Skia compressed 2D mip-chain dimensions must be in 1..16384.");
            if (error == SkiaCompressedMipChain2DLayoutError::InvalidBlockByteSize)
                throw std::invalid_argument("Skia compressed 2D mip-chain block byte size must be positive.");
            if (error == SkiaCompressedMipChain2DLayoutError::SizeOverflow)
            {
                throw System::NotSupportedException(
                    "Skia compressed 2D mip-chain storage size overflows the host address space.");
            }
            throw System::NotSupportedException(
                "Skia compressed 2D mip chain exceeds the 256 MiB per-resource CPU storage limit.");
        }

        storage_.assign(storageBytes, 0u);
        if (resourceCounters_)
        {
            resourceCounters_->AddMipChain2D(storageBytes);
            resourceRegistered_ = true;
        }
    }

    SkiaCompressedMipChain2D::~SkiaCompressedMipChain2D()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveMipChain2D(storage_.size());
    }

    const SkiaCompressedMipLevel2D& SkiaCompressedMipChain2D::Level(int level) const
    {
        if (level < 0 || level >= LevelCount())
            throw std::out_of_range("Skia compressed 2D mip-chain level is outside the allocated chain.");
        return levels_[static_cast<std::size_t>(level)];
    }

    std::uint8_t* SkiaCompressedMipChain2D::LevelData(int level)
    {
        return storage_.data() + Level(level).offset;
    }

    const std::uint8_t* SkiaCompressedMipChain2D::LevelData(int level) const
    {
        return storage_.data() + Level(level).offset;
    }
} // namespace CNA::Internal::Backends::Skia
