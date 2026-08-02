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
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        [[nodiscard]] bool IsSupportedPackedFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Color
                || format == SurfaceFormat::Bgr565
                || format == SurfaceFormat::Bgra4444
                || format == SurfaceFormat::Rgba1010102;
        }

        [[nodiscard]] std::size_t BytesPerTexel(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::Color:
                case SurfaceFormat::Rgba1010102:
                    return 4u;
                case SurfaceFormat::Bgr565:
                case SurfaceFormat::Bgra4444:
                    return 2u;
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no promoted representation for this SurfaceFormat.");
            }
        }

        [[nodiscard]] SkImageInfo PackedImageInfo(
            SurfaceFormat format, int width, int height, SkAlphaType alphaType)
        {
            switch (format)
            {
                case SurfaceFormat::Color:
                    return SkImageInfo::Make(
                        width, height, kRGBA_8888_SkColorType, alphaType);
                case SurfaceFormat::Bgr565:
                    return SkImageInfo::Make(
                        width, height, kRGB_565_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::Bgra4444:
                    // CNA stores A:R:G:B from the most to least significant nibble. Pinned
                    // kARGB_4444 stores R:G:B:A, so its bytes must never label CNA storage.
                    return SkImageInfo::Make(
                        width, height, kRGBA_8888_SkColorType, alphaType);
                case SurfaceFormat::Rgba1010102:
                    return SkImageInfo::Make(
                        width, height, kRGBA_1010102_SkColorType, alphaType);
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no image info for this SurfaceFormat.");
            }
        }

        [[nodiscard]] std::uint16_t ReadU16Le(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint16_t>(bytes[0])
                | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
        }

        [[nodiscard]] std::uint32_t ReadU32Le(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8u)
                | (static_cast<std::uint32_t>(bytes[2]) << 16u)
                | (static_cast<std::uint32_t>(bytes[3]) << 24u);
        }

        void WriteU16Le(std::uint8_t* bytes, std::uint16_t value) noexcept
        {
            bytes[0] = static_cast<std::uint8_t>(value);
            bytes[1] = static_cast<std::uint8_t>(value >> 8u);
        }

        void WriteU32Le(std::uint8_t* bytes, std::uint32_t value) noexcept
        {
            bytes[0] = static_cast<std::uint8_t>(value);
            bytes[1] = static_cast<std::uint8_t>(value >> 8u);
            bytes[2] = static_cast<std::uint8_t>(value >> 16u);
            bytes[3] = static_cast<std::uint8_t>(value >> 24u);
        }

        [[nodiscard]] std::vector<std::uint8_t> DecodeBgra4444(
            const std::uint8_t* source, int width, int height, std::size_t sourceRowBytes)
        {
            std::vector<std::uint8_t> rgba(
                static_cast<std::size_t>(width) * height * 4u);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::uint16_t word = ReadU16Le(
                        source + static_cast<std::size_t>(y) * sourceRowBytes
                        + static_cast<std::size_t>(x) * 2u);
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) * width + x) * 4u;
                    rgba[destination + 0u] = static_cast<std::uint8_t>(((word >> 8u) & 0xFu) * 17u);
                    rgba[destination + 1u] = static_cast<std::uint8_t>(((word >> 4u) & 0xFu) * 17u);
                    rgba[destination + 2u] = static_cast<std::uint8_t>((word & 0xFu) * 17u);
                    rgba[destination + 3u] = static_cast<std::uint8_t>(((word >> 12u) & 0xFu) * 17u);
                }
            }
            return rgba;
        }

        [[nodiscard]] sk_sp<SkImage> MakePackedImageCopy(
            SurfaceFormat format, const std::uint8_t* pixels, int width, int height,
            std::size_t rowBytes, SkAlphaType alphaType)
        {
            if (format == SurfaceFormat::Bgra4444)
            {
                const std::vector<std::uint8_t> rgba =
                    DecodeBgra4444(pixels, width, height, rowBytes);
                const SkPixmap pixmap(
                    PackedImageInfo(format, width, height, alphaType), rgba.data(),
                    static_cast<std::size_t>(width) * 4u);
                return SkImages::RasterFromPixmapCopy(pixmap);
            }
            const SkPixmap pixmap(
                PackedImageInfo(format, width, height, alphaType), pixels, rowBytes);
            return SkImages::RasterFromPixmapCopy(pixmap);
        }

        [[nodiscard]] sk_sp<SkImage> MakePackedMipImage(
            SurfaceFormat format, const std::uint8_t* pixels, int width, int height,
            std::size_t rowBytes, std::size_t byteCount, SkAlphaType alphaType)
        {
            if (format == SurfaceFormat::Bgra4444)
                return MakePackedImageCopy(format, pixels, width, height, rowBytes, alphaType);
            return SkImages::RasterFromData(
                PackedImageInfo(format, width, height, alphaType),
                SkData::MakeWithoutCopy(pixels, byteCount), rowBytes);
        }
    }

    SkiaTextureBackend::SkiaTextureBackend(const ImageData& data,
                                           std::shared_ptr<SkiaResourceCounters> resourceCounters)
        : width_(data.width)
        , height_(data.height)
        , format_(static_cast<SurfaceFormat>(data.surfaceFormat))
        , resourceCounters_(std::move(resourceCounters))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("Skia Texture2D dimensions must be positive.");
        if (!IsSupportedPackedFormat(format_))
            throw System::NotSupportedException(
                "Skia Texture2D SurfaceFormat has not passed its promotion gate.");
        bytesPerTexel_ = BytesPerTexel(format_);
        std::size_t requiredBytes = 0;
        if (!CheckedTexelBytes2D(static_cast<std::size_t>(width_),
                                 static_cast<std::size_t>(height_), bytesPerTexel_, requiredBytes)
            || requiredBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia Texture2D exceeds the checked 256 MiB per-resource CPU storage limit.");
        }
        if (data.pixels.size() != requiredBytes)
            throw std::runtime_error(
                "Skia Texture2D requires exactly width * height * format-size bytes.");

        std::vector<SkiaMipLevel2D> layout;
        std::size_t chainBytes = 0u;
        SkiaMipChain2DLayoutError layoutError = SkiaMipChain2DLayoutError::None;
        if (data.mipLevels <= 0
            || !TryBuildSkiaMipChain2DLayout(width_, height_, data.mipLevels > 1, bytesPerTexel_,
                                              layout, chainBytes, layoutError))
        {
            throw System::NotSupportedException(
                "Skia Texture2D cannot allocate the requested checked format mip chain.");
        }
        if (static_cast<int>(layout.size()) != data.mipLevels)
        {
            throw std::invalid_argument(
                "Skia Texture2D mipLevels must name level zero or the complete 2D mip chain.");
        }

        const std::size_t workingBytesPerTexel =
            format_ == SurfaceFormat::Bgra4444 ? 4u : bytesPerTexel_;
        std::size_t workingLevelZeroBytes = 0u;
        std::size_t imageBytes = 0u;
        std::size_t retainedBytes = 0u;
        if (!CheckedTexelBytes2D(static_cast<std::size_t>(width_),
                                 static_cast<std::size_t>(height_), workingBytesPerTexel,
                                 workingLevelZeroBytes)
            || !CheckedSizeMultiply(workingLevelZeroBytes, 2u, imageBytes)
            || !CheckedSizeAdd(chainBytes, imageBytes, retainedBytes)
            || retainedBytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia Texture2D mip storage and level-zero image views exceed the checked "
                "256 MiB per-resource limit.");
        }

        mipChain_ = std::make_unique<SkiaMipChain2D>(
            width_, height_, data.mipLevels > 1, bytesPerTexel_, resourceCounters_);
        std::memcpy(mipChain_->LevelData(0), data.pixels.data(), requiredBytes);
        authoredMipLevels_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), false);
        dirtyMipLevels_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), false);
        mipGenerationCounts_.assign(static_cast<std::size_t>(mipChain_->LevelCount()), 0u);
        authoredMipLevels_[0] = true;
        RebuildImage();
        if (resourceCounters_)
        {
            imageViewStorageBytes_ = imageBytes;
            resourceCounters_->AddTexture(imageViewStorageBytes_);
            resourceRegistered_ = true;
        }
    }

    SkiaTextureBackend::~SkiaTextureBackend()
    {
        if (resourceRegistered_)
            resourceCounters_->RemoveTexture(imageViewStorageBytes_);
    }

    void SkiaTextureBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (!rgba)
            throw std::runtime_error("Skia Texture2D UpdatePixels received null format data.");
        if (stride < static_cast<int>(static_cast<std::size_t>(width_) * bytesPerTexel_))
            throw std::runtime_error(
                "Skia Texture2D UpdatePixels stride is smaller than a format row.");

        for (int row = 0; row < height_; ++row)
        {
            std::memcpy(mipChain_->LevelData(0)
                            + static_cast<std::size_t>(row) * width_ * bytesPerTexel_,
                        rgba + static_cast<std::size_t>(row) * stride,
                        static_cast<std::size_t>(width_) * bytesPerTexel_);
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
            UpdatePixels(rgba, static_cast<int>(static_cast<std::size_t>(levelWidth)
                                                * bytesPerTexel_));
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
                                    static_cast<std::size_t>(height), bytesPerTexel_, requiredBytes)
            || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
        {
            return false;
        }

        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < height; ++row)
        {
            const auto sourceOffset =
                (static_cast<std::size_t>(y + row) * sourceLevel.width + x) * bytesPerTexel_;
            std::memcpy(destination + static_cast<std::size_t>(row) * width * bytesPerTexel_,
                        mipChain_->LevelData(level) + sourceOffset,
                        static_cast<std::size_t>(width) * bytesPerTexel_);
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
        const SkAlphaType alphaType =
            ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
                    == SkiaWorkingSourceRoute::PreserveDeclaredComponents
                ? kPremul_SkAlphaType
                : kUnpremul_SkAlphaType;
        // The chain has stable addresses and outlives this temporary image. No pixel copy or
        // retained per-draw cache is needed for direct layouts; UpdatePixels cannot overlap a
        // synchronous raster draw. Bgra4444 takes its mandatory bounded conversion copy here.
        return MakePackedMipImage(format_, mipChain_->LevelData(level), mip.width, mip.height,
                                  mip.rowBytes, mip.bytes, alphaType);
    }

    void SkiaTextureBackend::RebuildImage()
    {
        const SkiaMipLevel2D& levelZero = mipChain_->Level(0);
        straightImage_ = MakePackedImageCopy(
            format_, mipChain_->LevelData(0), width_, height_, levelZero.rowBytes,
            kUnpremul_SkAlphaType);
        premultipliedImage_ = MakePackedImageCopy(
            format_, mipChain_->LevelData(0), width_, height_, levelZero.rowBytes,
            kPremul_SkAlphaType);
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
        if (format_ == SurfaceFormat::Color)
        {
            GenerateSkiaRgba8MipLevel(*mipChain_, level);
            return;
        }

        const SkiaMipLevel2D& source = mipChain_->Level(level - 1);
        const SkiaMipLevel2D& target = mipChain_->Level(level);
        const std::uint8_t* sourcePixels = mipChain_->LevelData(level - 1);
        std::uint8_t* targetPixels = mipChain_->LevelData(level);
        for (int y = 0; y < target.height; ++y)
        {
            const int y0 = static_cast<int>(
                static_cast<std::int64_t>(y) * source.height / target.height);
            const int y1 = static_cast<int>(
                static_cast<std::int64_t>(y + 1) * source.height / target.height);
            for (int x = 0; x < target.width; ++x)
            {
                const int x0 = static_cast<int>(
                    static_cast<std::int64_t>(x) * source.width / target.width);
                const int x1 = static_cast<int>(
                    static_cast<std::int64_t>(x + 1) * source.width / target.width);
                const std::uint32_t sampleCount = static_cast<std::uint32_t>(
                    (x1 - x0) * (y1 - y0));
                std::uint64_t sums[4] = {0u, 0u, 0u, 0u};
                for (int sourceY = y0; sourceY < y1; ++sourceY)
                {
                    for (int sourceX = x0; sourceX < x1; ++sourceX)
                    {
                        const std::uint8_t* texel = sourcePixels
                            + static_cast<std::size_t>(sourceY) * source.rowBytes
                            + static_cast<std::size_t>(sourceX) * bytesPerTexel_;
                        if (format_ == SurfaceFormat::Bgr565)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 11u) & 0x1Fu;
                            sums[1] += (word >> 5u) & 0x3Fu;
                            sums[2] += word & 0x1Fu;
                        }
                        else if (format_ == SurfaceFormat::Bgra4444)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 8u) & 0xFu;
                            sums[1] += (word >> 4u) & 0xFu;
                            sums[2] += word & 0xFu;
                            sums[3] += (word >> 12u) & 0xFu;
                        }
                        else
                        {
                            const std::uint32_t word = ReadU32Le(texel);
                            sums[0] += word & 0x3FFu;
                            sums[1] += (word >> 10u) & 0x3FFu;
                            sums[2] += (word >> 20u) & 0x3FFu;
                            sums[3] += (word >> 30u) & 0x3u;
                        }
                    }
                }

                auto average = [sampleCount](std::uint64_t sum) -> std::uint32_t {
                    return static_cast<std::uint32_t>(
                        (sum + sampleCount / 2u) / sampleCount);
                };
                std::uint8_t* output = targetPixels
                    + static_cast<std::size_t>(y) * target.rowBytes
                    + static_cast<std::size_t>(x) * bytesPerTexel_;
                if (format_ == SurfaceFormat::Bgr565)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[0]) << 11u) | (average(sums[1]) << 5u)
                        | average(sums[2])));
                }
                else if (format_ == SurfaceFormat::Bgra4444)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[3]) << 12u) | (average(sums[0]) << 8u)
                        | (average(sums[1]) << 4u) | average(sums[2])));
                }
                else
                {
                    WriteU32Le(output,
                        average(sums[0]) | (average(sums[1]) << 10u)
                        | (average(sums[2]) << 20u) | (average(sums[3]) << 30u));
                }
            }
        }
    }
} // namespace CNA::Internal::Backends::Skia
