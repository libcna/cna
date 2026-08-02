#include "CNA/Internal/Backends/Skia/SkiaMipChain2D.hpp"

#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    bool TryBuildSkiaMipChain2DLayout(
        int width, int height, bool mipMap, std::size_t bytesPerTexel,
        std::vector<SkiaMipLevel2D>& levels, std::size_t& storageBytes,
        SkiaMipChain2DLayoutError& error)
    {
        if (!IsValidSkiaResourceAxis(width) || !IsValidSkiaResourceAxis(height))
        {
            error = SkiaMipChain2DLayoutError::InvalidDimensions;
            return false;
        }
        if (bytesPerTexel == 0u)
        {
            error = SkiaMipChain2DLayoutError::InvalidBytesPerTexel;
            return false;
        }

        std::vector<SkiaMipLevel2D> builtLevels;
        int levelWidth = width;
        int levelHeight = height;
        std::size_t total = 0u;
        do
        {
            std::size_t rowBytes = 0u;
            std::size_t levelBytes = 0u;
            if (!CheckedSizeMultiply(static_cast<std::size_t>(levelWidth), bytesPerTexel,
                                     rowBytes)
                || !CheckedSizeMultiply(rowBytes, static_cast<std::size_t>(levelHeight),
                                        levelBytes))
            {
                error = SkiaMipChain2DLayoutError::SizeOverflow;
                return false;
            }

            std::size_t nextTotal = 0u;
            if (!CheckedSizeAdd(total, levelBytes, nextTotal))
            {
                error = SkiaMipChain2DLayoutError::SizeOverflow;
                return false;
            }
            if (nextTotal > kSkiaCpuTextureStorageLimitBytes)
            {
                error = SkiaMipChain2DLayoutError::BudgetExceeded;
                return false;
            }

            builtLevels.push_back({levelWidth, levelHeight, rowBytes, total, levelBytes});
            total = nextTotal;
            levelWidth = std::max(1, levelWidth / 2);
            levelHeight = std::max(1, levelHeight / 2);
        }
        while (mipMap
               && (builtLevels.back().width > 1 || builtLevels.back().height > 1));

        levels = std::move(builtLevels);
        storageBytes = total;
        error = SkiaMipChain2DLayoutError::None;
        return true;
    }

    SkiaMipChain2D::SkiaMipChain2D(
        int width, int height, bool mipMap, std::size_t bytesPerTexel,
        std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : resourceCounters_(std::move(resourceCounters))
    {
        std::size_t storageBytes = 0u;
        SkiaMipChain2DLayoutError error = SkiaMipChain2DLayoutError::None;
        if (!TryBuildSkiaMipChain2DLayout(width, height, mipMap, bytesPerTexel,
                                          levels_, storageBytes, error))
        {
            if (error == SkiaMipChain2DLayoutError::InvalidDimensions)
                throw std::invalid_argument("Skia 2D mip-chain dimensions must be in 1..16384.");
            if (error == SkiaMipChain2DLayoutError::InvalidBytesPerTexel)
                throw std::invalid_argument("Skia 2D mip-chain bytes per texel must be positive.");
            if (error == SkiaMipChain2DLayoutError::SizeOverflow)
            {
                throw System::NotSupportedException(
                    "Skia 2D mip-chain storage size overflows the host address space.");
            }
            throw System::NotSupportedException(
                "Skia 2D mip chain exceeds the 256 MiB per-resource CPU storage limit.");
        }

        storage_.assign(storageBytes, 0u);
        if (resourceCounters_)
        {
            resourceCounters_->AddMipChain2D(storageBytes);
            resourceRegistered_ = true;
        }
    }

    SkiaMipChain2D::~SkiaMipChain2D()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveMipChain2D(storage_.size());
    }

    const SkiaMipLevel2D& SkiaMipChain2D::Level(int level) const
    {
        if (level < 0 || level >= LevelCount())
            throw std::out_of_range("Skia 2D mip-chain level is outside the allocated chain.");
        return levels_[static_cast<std::size_t>(level)];
    }

    std::uint8_t* SkiaMipChain2D::LevelData(int level)
    {
        return storage_.data() + Level(level).offset;
    }

    const std::uint8_t* SkiaMipChain2D::LevelData(int level) const
    {
        return storage_.data() + Level(level).offset;
    }

    void GenerateSkiaRgba8MipLevel(SkiaMipChain2D& chain, int level)
    {
        if (level <= 0 || level >= chain.LevelCount())
            throw std::out_of_range("Skia RGBA8 mip generation requires a valid descendant level.");

        const SkiaMipLevel2D& source = chain.Level(level - 1);
        const SkiaMipLevel2D& target = chain.Level(level);
        if (source.rowBytes != static_cast<std::size_t>(source.width) * 4u
            || target.rowBytes != static_cast<std::size_t>(target.width) * 4u)
        {
            throw std::invalid_argument("Skia RGBA8 mip generation requires four-byte texels.");
        }

        const std::uint8_t* sourcePixels = chain.LevelData(level - 1);
        std::uint8_t* targetPixels = chain.LevelData(level);

        // Integer area boxes partition the complete odd/NPOT source. For example 7 -> 3 uses
        // [0,2), [2,4), [4,7), so the final edge contributes exactly once instead of being
        // dropped. Canonical straight RGBA bytes are averaged independently; Skia alpha
        // conversion is not involved in mip generation.
        for (int y = 0; y < target.height; ++y)
        {
            const int sourceY0 = static_cast<int>(
                static_cast<std::int64_t>(y) * source.height / target.height);
            const int sourceY1 = static_cast<int>(
                static_cast<std::int64_t>(y + 1) * source.height / target.height);
            for (int x = 0; x < target.width; ++x)
            {
                const int sourceX0 = static_cast<int>(
                    static_cast<std::int64_t>(x) * source.width / target.width);
                const int sourceX1 = static_cast<int>(
                    static_cast<std::int64_t>(x + 1) * source.width / target.width);
                const unsigned int sampleCount = static_cast<unsigned int>(
                    (sourceX1 - sourceX0) * (sourceY1 - sourceY0));
                for (int channel = 0; channel < 4; ++channel)
                {
                    unsigned int sum = 0u;
                    for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY)
                    {
                        for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                        {
                            const std::size_t sourceOffset =
                                static_cast<std::size_t>(sourceY) * source.rowBytes
                                + static_cast<std::size_t>(sourceX) * 4u
                                + static_cast<std::size_t>(channel);
                            sum += sourcePixels[sourceOffset];
                        }
                    }
                    const std::size_t targetOffset =
                        static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * 4u
                        + static_cast<std::size_t>(channel);
                    targetPixels[targetOffset] = static_cast<std::uint8_t>(
                        (sum + sampleCount / 2u) / sampleCount);
                }
            }
        }
    }
} // namespace CNA::Internal::Backends::Skia
