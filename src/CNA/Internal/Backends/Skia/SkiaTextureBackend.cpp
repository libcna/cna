#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

#include "include/core/SkImageInfo.h"
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
} // namespace CNA::Internal::Backends::Skia
