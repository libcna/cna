#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"

#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

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

    SkiaTextureBackend::SkiaTextureBackend(const ImageData& data)
        : width_(data.width)
        , height_(data.height)
        , rawPixels_(data.pixels)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Skia Texture2D dimensions must be positive.");
        const std::size_t requiredBytes = static_cast<std::size_t>(width_) * height_ * 4u;
        if (rawPixels_.size() != requiredBytes)
            throw std::runtime_error("Skia Texture2D requires exactly width * height * 4 RGBA8 bytes.");
        if (data.mipLevels != 1)
            throw System::NotSupportedException(
                "Skia raster Texture2D does not implement public mip chains; mipMap=true is rejected "
                "before texture data can be uploaded.");
        RebuildImage();
    }

    void SkiaTextureBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!rgba)
            throw std::runtime_error("Skia Texture2D UpdatePixels received null RGBA data.");
        if (stride < width_ * 4)
            throw std::runtime_error("Skia Texture2D UpdatePixels stride is smaller than an RGBA row.");

        for (int row = 0; row < height_; ++row)
        {
            std::memcpy(rawPixels_.data() + static_cast<std::size_t>(row) * width_ * 4u,
                        rgba + static_cast<std::size_t>(row) * stride,
                        static_cast<std::size_t>(width_) * 4u);
        }
        RebuildImage();
    }

    void SkiaTextureBackend::UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                                                int levelWidth, int levelHeight)
    {
        if (level != 0)
            throw std::runtime_error("Skia raster Texture2D does not implement mip-level uploads.");
        if (levelWidth != width_ || levelHeight != height_)
            throw std::runtime_error("Skia level-0 Texture2D upload dimensions do not match the texture.");
        UpdatePixels(rgba, width_ * 4);
    }

    bool SkiaTextureBackend::GetData(int level, int x, int y, int width, int height,
                                     void* data, int dataLength) const
    {
        if (level != 0 || !data || width < 0 || height < 0 || x < 0 || y < 0
            || x > width_ - width || y > height_ - height
            || dataLength < width * height * 4)
        {
            return false;
        }

        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const auto sourceOffset = (static_cast<std::size_t>(y + row) * width_ + x) * 4u;
            std::memcpy(destination + static_cast<std::size_t>(row) * width * 4u,
                        rawPixels_.data() + sourceOffset, static_cast<std::size_t>(width) * 4u);
        }
        return true;
    }

    sk_sp<SkImage> SkiaTextureBackend::SnapshotImage(
        SkiaSourceAlphaConvention alphaConvention) const
    {
        return alphaConvention == SkiaSourceAlphaConvention::Premultiplied
            ? premultipliedImage_
            : straightImage_;
    }

    void SkiaTextureBackend::RebuildImage()
    {
        const SkPixmap straightPixmap(RgbaUnpremulInfo(width_, height_), rawPixels_.data(), width_ * 4);
        const SkPixmap premultipliedPixmap(RgbaPremulInfo(width_, height_), rawPixels_.data(), width_ * 4);
        straightImage_ = SkImages::RasterFromPixmapCopy(straightPixmap);
        premultipliedImage_ = SkImages::RasterFromPixmapCopy(premultipliedPixmap);
        if (!straightImage_ || !premultipliedImage_)
            throw std::runtime_error("Skia failed to create a raster Texture2D image.");
    }
} // namespace CNA::Internal::Backends::Skia
