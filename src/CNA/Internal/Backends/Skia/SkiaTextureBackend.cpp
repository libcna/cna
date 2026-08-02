#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include "include/core/SkImageInfo.h"
#include "include/core/SkData.h"
#include "include/core/SkPixmap.h"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] SkImageInfo RgbaUnpremulInfo(int width, int height)
        {
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        }

        [[nodiscard]] SkImageInfo RgbaPremulInfo(int width, int height)
        {
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        }
    }

    SkiaTextureBackend::SkiaTextureBackend(const ImageData& data,
                                           std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : width_(data.width)
        , height_(data.height)
        , resourceCounters_(std::move(resourceCounters))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Skia Texture2D dimensions must be positive.");
        std::size_t requiredBytes = 0;
        if (!CheckedTexelBytes2D(static_cast<std::size_t>(width_),
                                 static_cast<std::size_t>(height_), 4u, requiredBytes)
            || requiredBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia Texture2D exceeds the checked 256 MiB per-resource CPU storage limit.");
        }
        if (data.pixels.size() != requiredBytes)
            throw std::runtime_error("Skia Texture2D requires exactly width * height * 4 RGBA8 bytes.");

        std::vector<SkiaMipLevel2D> layout;
        std::size_t chainBytes = 0u;
        SkiaMipChain2DLayoutError layoutError = SkiaMipChain2DLayoutError::None;
        if (data.mipLevels <= 0
            || !TryBuildSkiaMipChain2DLayout(width_, height_, data.mipLevels > 1, 4u,
                                              layout, chainBytes, layoutError))
        {
            throw System::NotSupportedException(
                "Skia Texture2D cannot allocate the requested checked RGBA8 mip chain.");
        }
        if (static_cast<int>(layout.size()) != data.mipLevels)
        {
            throw std::invalid_argument(
                "Skia Texture2D mipLevels must name level zero or the complete 2D mip chain.");
        }

        std::size_t imageBytes = 0u;
        std::size_t retainedBytes = 0u;
        if (!CheckedSizeMultiply(requiredBytes, 2u, imageBytes)
            || !CheckedSizeAdd(chainBytes, imageBytes, retainedBytes)
            || retainedBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia Texture2D mip storage and level-zero image views exceed the checked "
                "256 MiB per-resource limit.");
        }

        mipChain_ = std::make_unique<SkiaMipChain2D>(
            width_, height_, data.mipLevels > 1, 4u, resourceCounters_);
        std::memcpy(mipChain_->LevelData(0), data.pixels.data(), requiredBytes);
        authoredMipLevels_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), false);
        dirtyMipLevels_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), false);
        mipGenerationCounts_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), 0u);
        authoredMipLevels_[0] = true;
        RebuildImage();
        if (resourceCounters_)
        {
            resourceCounters_->AddTexture(width_, height_);
            resourceRegistered_ = true;
        }
    }

    SkiaTextureBackend::~SkiaTextureBackend()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveTexture(width_, height_);
    }

    void SkiaTextureBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!rgba)
            throw std::runtime_error("Skia Texture2D UpdatePixels received null RGBA data.");
        if (stride < width_ * 4)
            throw std::runtime_error("Skia Texture2D UpdatePixels stride is smaller than an RGBA row.");

        for (int row = 0; row < height_; ++row)
        {
            std::memcpy(mipChain_->LevelData(0) + static_cast<std::size_t>(row) * width_ * 4u,
                        rgba + static_cast<std::size_t>(row) * stride,
                        static_cast<std::size_t>(width_) * 4u);
        }
        RebuildImage();
        InvalidateGeneratedDescendants(0);
        GenerateDirtyMipLevels();
    }

    void SkiaTextureBackend::UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                                                int levelWidth, int levelHeight)
    {
        if (!rgba)
            throw std::runtime_error("Skia Texture2D mip upload received null RGBA data.");
        const SkiaMipLevel2D& target = mipChain_->Level(level);
        if (levelWidth != target.width || levelHeight != target.height)
        {
            throw std::runtime_error(
                "Skia Texture2D mip upload dimensions do not match the requested level.");
        }
        if (level == 0)
        {
            UpdatePixels(rgba, levelWidth * 4);
            return;
        }
        std::memcpy(mipChain_->LevelData(level), rgba, target.bytes);
        authoredMipLevels_[static_cast<std::size_t>(level)] = true;
        dirtyMipLevels_[static_cast<std::size_t>(level)] = false;
        InvalidateGeneratedDescendants(level);
        GenerateDirtyMipLevels();
    }

    bool SkiaTextureBackend::GetData(int level, int x, int y, int width, int height,
                                     void* data, int dataLength) const
    {
        if (!data || level < 0 || level >= mipChain_->LevelCount())
            return false;
        const SkiaMipLevel2D& sourceLevel = mipChain_->Level(level);
        std::size_t requiredBytes = 0;
        if (width <= 0 || height <= 0 || x < 0 || y < 0
            || width > sourceLevel.width || height > sourceLevel.height
            || x > sourceLevel.width - width || y > sourceLevel.height - height
            || !CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height), 4u, requiredBytes)
            || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
        {
            return false;
        }

        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const auto sourceOffset =
                (static_cast<std::size_t>(y + row) * sourceLevel.width + x) * 4u;
            std::memcpy(destination + static_cast<std::size_t>(row) * width * 4u,
                        mipChain_->LevelData(level) + sourceOffset,
                        static_cast<std::size_t>(width) * 4u);
        }
        return true;
    }

    sk_sp<SkImage> SkiaTextureBackend::SnapshotImage(
        SkiaSourceAlphaConvention alphaConvention) const
    {
        return ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
                == SkiaWorkingSourceRoute::PreserveDeclaredComponents
            ? premultipliedImage_
            : straightImage_;
    }

    sk_sp<SkImage> SkiaTextureBackend::SnapshotMipLevelEXT(
        int level, SkiaSourceAlphaConvention alphaConvention) const
    {
        if (level < 0 || level >= mipChain_->LevelCount())
            return nullptr;
        if (level == 0)
            return SnapshotImage(alphaConvention);

        const SkiaMipLevel2D& mip = mipChain_->Level(level);
        const SkImageInfo info = ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
                == SkiaWorkingSourceRoute::PreserveDeclaredComponents
            ? RgbaPremulInfo(mip.width, mip.height)
            : RgbaUnpremulInfo(mip.width, mip.height);
        // The chain has stable addresses and outlives this temporary image. No pixel copy or
        // retained per-draw cache is needed; UpdatePixels cannot overlap a synchronous raster draw.
        return SkImages::RasterFromData(
            info, SkData::MakeWithoutCopy(mipChain_->LevelData(level), mip.bytes), mip.rowBytes);
    }

    void SkiaTextureBackend::RebuildImage()
    {
        const SkPixmap straightPixmap(
            RgbaUnpremulInfo(width_, height_), mipChain_->LevelData(0), width_ * 4);
        const SkPixmap premultipliedPixmap(
            RgbaPremulInfo(width_, height_), mipChain_->LevelData(0), width_ * 4);
        straightImage_ = SkImages::RasterFromPixmapCopy(straightPixmap);
        premultipliedImage_ = SkImages::RasterFromPixmapCopy(premultipliedPixmap);
        if (!straightImage_ || !premultipliedImage_)
            throw std::runtime_error("Skia failed to create a raster Texture2D image.");
    }

    std::uint64_t SkiaTextureBackend::MipGenerationCountEXT(int level) const
    {
        if (level < 0 || level >= static_cast<int>(mipGenerationCounts_.size()))
            throw std::out_of_range("Skia Texture2D mip generation level is out of range.");
        return mipGenerationCounts_[static_cast<std::size_t>(level)];
    }

    void SkiaTextureBackend::InvalidateGeneratedDescendants(int level)
    {
        for (int descendant = level + 1; descendant < mipChain_->LevelCount(); ++descendant)
        {
            if (authoredMipLevels_[static_cast<std::size_t>(descendant)])
                break;
            dirtyMipLevels_[static_cast<std::size_t>(descendant)] = true;
        }
    }

    void SkiaTextureBackend::GenerateDirtyMipLevels()
    {
        for (int level = 1; level < mipChain_->LevelCount(); ++level)
        {
            if (!dirtyMipLevels_[static_cast<std::size_t>(level)])
                continue;
            GenerateMipLevel(level);
            dirtyMipLevels_[static_cast<std::size_t>(level)] = false;
            ++mipGenerationCounts_[static_cast<std::size_t>(level)];
        }
    }

    void SkiaTextureBackend::GenerateMipLevel(int level)
    {
        const SkiaMipLevel2D& source = mipChain_->Level(level - 1);
        const SkiaMipLevel2D& target = mipChain_->Level(level);
        const std::uint8_t* sourcePixels = mipChain_->LevelData(level - 1);
        std::uint8_t* targetPixels = mipChain_->LevelData(level);

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
