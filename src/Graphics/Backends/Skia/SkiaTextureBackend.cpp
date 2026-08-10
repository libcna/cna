#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaMipGeneration.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"
#include "CNA/Internal/Graphics/Bc7Util.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkData.h"
#include "include/core/SkPixmap.h"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    namespace
    {
        [[nodiscard]] bool IsSupportedTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Color
                || format == SurfaceFormat::Bgr565
                || format == SurfaceFormat::Bgra5551
                || format == SurfaceFormat::Bgra4444
                || format == SurfaceFormat::Rgba1010102
                || format == SurfaceFormat::Rg32
                || format == SurfaceFormat::Rgba64
                || format == SurfaceFormat::Alpha8
                || format == SurfaceFormat::ColorBgraEXT
                || format == SurfaceFormat::ColorSrgbEXT
                || format == SurfaceFormat::ByteEXT
                || format == SurfaceFormat::UShortEXT
                || format == SurfaceFormat::Single
                || format == SurfaceFormat::Vector2
                || format == SurfaceFormat::Vector4
                || format == SurfaceFormat::HalfSingle
                || format == SurfaceFormat::HalfVector2
                || format == SurfaceFormat::HalfVector4
                || format == SurfaceFormat::NormalizedByte2
                || format == SurfaceFormat::NormalizedByte4
                || format == SurfaceFormat::HdrBlendable
                || format == SurfaceFormat::Dxt1
                || format == SurfaceFormat::Dxt3
                || format == SurfaceFormat::Dxt5
                || format == SurfaceFormat::Bc7EXT
                || format == SurfaceFormat::Bc7SrgbEXT;
        }

        [[nodiscard]] bool IsCompressedTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Dxt1
                || format == SurfaceFormat::Dxt3
                || format == SurfaceFormat::Dxt5
                || format == SurfaceFormat::Bc7EXT
                || format == SurfaceFormat::Bc7SrgbEXT;
        }

        [[nodiscard]] bool IsSrgbCompressedTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Bc7SrgbEXT;
        }

        [[nodiscard]] std::size_t CompressedBlockByteSize(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::Dxt1: return 8u;
                case SurfaceFormat::Dxt3:
                case SurfaceFormat::Dxt5:
                case SurfaceFormat::Bc7EXT:
                case SurfaceFormat::Bc7SrgbEXT: return 16u;
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no block byte size for this SurfaceFormat.");
            }
        }

        [[nodiscard]] std::vector<std::uint8_t> DecompressBlockLevel(
            SurfaceFormat format, const std::uint8_t* data, std::size_t dataSize,
            int width, int height)
        {
            using CNA::Internal::Graphics::Bc7Util;
            using CNA::Internal::Graphics::DxtUtil;
            switch (format)
            {
                case SurfaceFormat::Dxt1: return DxtUtil::DecompressDxt1(data, dataSize, width, height);
                case SurfaceFormat::Dxt3: return DxtUtil::DecompressDxt3(data, dataSize, width, height);
                case SurfaceFormat::Dxt5: return DxtUtil::DecompressDxt5(data, dataSize, width, height);
                case SurfaceFormat::Bc7EXT:
                case SurfaceFormat::Bc7SrgbEXT:
                    return Bc7Util::DecompressBc7(data, dataSize, width, height);
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no block decoder for this SurfaceFormat.");
            }
        }

        [[nodiscard]] sk_sp<SkImage> MakeDecodedCompressedImage(
            SurfaceFormat format, const std::uint8_t* data, std::size_t dataSize,
            int width, int height, SkAlphaType alphaType)
        {
            const std::vector<std::uint8_t> rgba =
                DecompressBlockLevel(format, data, dataSize, width, height);
            // Bc7SrgbEXT's decoded RGB bytes are sRGB-encoded; kSRGBA_8888 performs the sRGB
            // transfer decode while gathering texels, and the gathered working components are
            // described as linear-sRGB so a linear destination does not decode them a second
            // time while an explicit sRGB destination re-encodes them exactly once -- the same
            // convention already established for ColorSrgbEXT.
            const SkImageInfo info = IsSrgbCompressedTextureFormat(format)
                ? SkImageInfo::Make(width, height, kSRGBA_8888_SkColorType, alphaType,
                                    SkColorSpace::MakeSRGBLinear())
                : SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, alphaType);
            const SkPixmap pixmap(info, rgba.data(), static_cast<std::size_t>(width) * 4u);
            return SkImages::RasterFromPixmapCopy(pixmap);
        }

        [[nodiscard]] std::size_t BytesPerTexel(SurfaceFormat format)
        {
            switch (format)
            {
                case SurfaceFormat::Color:
                case SurfaceFormat::Rgba1010102:
                case SurfaceFormat::Rg32:
                case SurfaceFormat::ColorBgraEXT:
                case SurfaceFormat::ColorSrgbEXT:
                case SurfaceFormat::Single:
                case SurfaceFormat::HalfVector2:
                case SurfaceFormat::NormalizedByte4:
                    return 4u;
                case SurfaceFormat::Bgr565:
                case SurfaceFormat::Bgra5551:
                case SurfaceFormat::Bgra4444:
                case SurfaceFormat::UShortEXT:
                case SurfaceFormat::HalfSingle:
                case SurfaceFormat::NormalizedByte2:
                    return 2u;
                case SurfaceFormat::Rgba64:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                    return 8u;
                case SurfaceFormat::Alpha8:
                case SurfaceFormat::ByteEXT:
                    return 1u;
                case SurfaceFormat::Vector4:
                    return 16u;
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no promoted representation for this SurfaceFormat.");
            }
        }

        [[nodiscard]] SkImageInfo TextureImageInfo(
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
                case SurfaceFormat::Bgra5551:
                case SurfaceFormat::NormalizedByte4:
                    return SkImageInfo::Make(
                        width, height, kRGBA_F32_SkColorType, alphaType);
                case SurfaceFormat::Bgra4444:
                    // CNA stores A:R:G:B from the most to least significant nibble. Pinned
                    // kARGB_4444 stores R:G:B:A, so its bytes must never label CNA storage.
                    return SkImageInfo::Make(
                        width, height, kRGBA_8888_SkColorType, alphaType);
                case SurfaceFormat::NormalizedByte2:
                    return SkImageInfo::Make(
                        width, height, kRGBA_F32_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::Rgba1010102:
                    return SkImageInfo::Make(
                        width, height, kRGBA_1010102_SkColorType, alphaType);
                case SurfaceFormat::Rg32:
                    return SkImageInfo::Make(
                        width, height, kR16G16_unorm_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::Rgba64:
                    return SkImageInfo::Make(
                        width, height, kR16G16B16A16_unorm_SkColorType, alphaType);
                case SurfaceFormat::Alpha8:
                    return SkImageInfo::Make(
                        width, height, kAlpha_8_SkColorType, kPremul_SkAlphaType);
                case SurfaceFormat::ColorBgraEXT:
                    return SkImageInfo::Make(
                        width, height, kBGRA_8888_SkColorType, alphaType);
                case SurfaceFormat::ColorSrgbEXT:
                    // kSRGBA_8888 performs the sRGB transfer decode while gathering texels.
                    // Describe the gathered working components as linear-sRGB so drawing to a
                    // linear destination does not decode them a second time, while an explicit
                    // sRGB destination re-encodes them exactly once.
                    return SkImageInfo::Make(
                        width, height, kSRGBA_8888_SkColorType, alphaType,
                        SkColorSpace::MakeSRGBLinear());
                case SurfaceFormat::ByteEXT:
                    return SkImageInfo::Make(
                        width, height, kR8_unorm_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::UShortEXT:
                    return SkImageInfo::Make(
                        width, height, kR16_unorm_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::Single:
                case SurfaceFormat::Vector2:
                case SurfaceFormat::Vector4:
                    return SkImageInfo::Make(
                        width, height, kRGBA_F32_SkColorType,
                        format == SurfaceFormat::Vector4 ? alphaType : kOpaque_SkAlphaType);
                case SurfaceFormat::HalfSingle:
                    return SkImageInfo::Make(
                        width, height, kR16_float_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::HalfVector2:
                    return SkImageInfo::Make(
                        width, height, kR16G16_float_SkColorType, kOpaque_SkAlphaType);
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                    return SkImageInfo::Make(
                        width, height, kRGBA_F16_SkColorType, alphaType);
                default:
                    throw System::NotSupportedException(
                        "Skia Texture2D has no image info for this SurfaceFormat.");
            }
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

        [[nodiscard]] bool NeedsDecodedRgbaF32Image(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Bgra5551
                || format == SurfaceFormat::NormalizedByte2
                || format == SurfaceFormat::NormalizedByte4;
        }

        [[nodiscard]] float DecodeSnormByte(std::uint8_t value) noexcept
        {
            return static_cast<float>(std::max(-127, DecodeSignedByte(value))) / 127.0f;
        }

        void WriteFloatLe(std::uint8_t* bytes, float value) noexcept
        {
            WriteU32Le(bytes, std::bit_cast<std::uint32_t>(value));
        }

        [[nodiscard]] std::vector<std::uint8_t> DecodeRgbaF32Shadow(
            SurfaceFormat format, const std::uint8_t* source, int width, int height,
            std::size_t sourceRowBytes)
        {
            const std::size_t sourceTexelBytes =
                format == SurfaceFormat::NormalizedByte4 ? 4u : 2u;
            std::vector<std::uint8_t> rgba(
                static_cast<std::size_t>(width) * height * 16u);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t* input = source
                        + static_cast<std::size_t>(y) * sourceRowBytes
                        + static_cast<std::size_t>(x) * sourceTexelBytes;
                    std::uint8_t* output = rgba.data()
                        + (static_cast<std::size_t>(y) * width + x) * 16u;
                    if (format == SurfaceFormat::Bgra5551)
                    {
                        const std::uint16_t word = ReadU16Le(input);
                        WriteFloatLe(
                            output + 0u, static_cast<float>((word >> 10u) & 0x1Fu) / 31.0f);
                        WriteFloatLe(
                            output + 4u, static_cast<float>((word >> 5u) & 0x1Fu) / 31.0f);
                        WriteFloatLe(
                            output + 8u, static_cast<float>(word & 0x1Fu) / 31.0f);
                        WriteFloatLe(output + 12u, (word & 0x8000u) != 0u ? 1.0f : 0.0f);
                        continue;
                    }
                    const int channelCount =
                        format == SurfaceFormat::NormalizedByte2 ? 2 : 4;
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        WriteFloatLe(output + static_cast<std::size_t>(channel) * 4u,
                                     DecodeSnormByte(input[channel]));
                    }
                    if (channelCount == 2)
                    {
                        WriteFloatLe(output + 8u, 0.0f);
                        WriteFloatLe(output + 12u, 1.0f);
                    }
                }
            }
            return rgba;
        }

        [[nodiscard]] bool NeedsExpandedFloatImage(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Single || format == SurfaceFormat::Vector2;
        }

        [[nodiscard]] std::vector<std::uint8_t> ExpandFloatImage(
            SurfaceFormat format, const std::uint8_t* source, int width, int height,
            std::size_t sourceRowBytes)
        {
            const std::size_t sourceTexelBytes =
                format == SurfaceFormat::Single ? 4u : 8u;
            std::vector<std::uint8_t> rgba(
                static_cast<std::size_t>(width) * height * 16u, 0u);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::uint8_t* input = source
                        + static_cast<std::size_t>(y) * sourceRowBytes
                        + static_cast<std::size_t>(x) * sourceTexelBytes;
                    std::uint8_t* output = rgba.data()
                        + (static_cast<std::size_t>(y) * width + x) * 16u;
                    std::memcpy(output, input, sourceTexelBytes);
                    WriteU32Le(output + 12u, 0x3F800000u);
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
                    TextureImageInfo(format, width, height, alphaType), rgba.data(),
                    static_cast<std::size_t>(width) * 4u);
                return SkImages::RasterFromPixmapCopy(pixmap);
            }
            if (NeedsDecodedRgbaF32Image(format))
            {
                const std::vector<std::uint8_t> rgba =
                    DecodeRgbaF32Shadow(format, pixels, width, height, rowBytes);
                const SkPixmap pixmap(
                    TextureImageInfo(format, width, height, alphaType), rgba.data(),
                    static_cast<std::size_t>(width) * 16u);
                return SkImages::RasterFromPixmapCopy(pixmap);
            }
            if (NeedsExpandedFloatImage(format))
            {
                const std::vector<std::uint8_t> rgba =
                    ExpandFloatImage(format, pixels, width, height, rowBytes);
                const SkPixmap pixmap(
                    TextureImageInfo(format, width, height, alphaType), rgba.data(),
                    static_cast<std::size_t>(width) * 16u);
                return SkImages::RasterFromPixmapCopy(pixmap);
            }
            const SkPixmap pixmap(
                TextureImageInfo(format, width, height, alphaType), pixels, rowBytes);
            return SkImages::RasterFromPixmapCopy(pixmap);
        }

        [[nodiscard]] sk_sp<SkImage> MakePackedMipImage(
            SurfaceFormat format, const std::uint8_t* pixels, int width, int height,
            std::size_t rowBytes, std::size_t byteCount, SkAlphaType alphaType)
        {
            if (format == SurfaceFormat::Bgra4444 || NeedsExpandedFloatImage(format)
                || NeedsDecodedRgbaF32Image(format))
                return MakePackedImageCopy(format, pixels, width, height, rowBytes, alphaType);
            return SkImages::RasterFromData(
                TextureImageInfo(format, width, height, alphaType),
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
        if (!IsSupportedTextureFormat(format_))
            throw System::NotSupportedException(
                "Skia Texture2D SurfaceFormat has not passed its promotion gate.");
        if (IsCompressedTextureFormat(format_))
        {
            const std::size_t blockByteSize = CompressedBlockByteSize(format_);
            std::vector<SkiaCompressedMipLevel2D> layout;
            std::size_t chainBytes = 0u;
            SkiaCompressedMipChain2DLayoutError layoutError =
                SkiaCompressedMipChain2DLayoutError::None;
            if (data.mipLevels <= 0
                || !TryBuildSkiaCompressedMipChain2DLayout(width_, height_, data.mipLevels > 1,
                                                            blockByteSize, layout, chainBytes,
                                                            layoutError))
            {
                throw System::NotSupportedException(
                    "Skia Texture2D cannot allocate the requested compressed format mip chain.");
            }
            if (static_cast<int>(layout.size()) != data.mipLevels)
            {
                throw std::invalid_argument(
                    "Skia Texture2D mipLevels must name level zero or the complete compressed "
                    "mip chain.");
            }
            if (data.pixels.size() != layout[0].bytes)
            {
                throw std::runtime_error(
                    "Skia Texture2D requires exactly the padded compressed block bytes for "
                    "level zero.");
            }

            std::size_t imageBytes = 0u;
            std::size_t retainedBytes = 0u;
            if (!CheckedTexelBytes2D(static_cast<std::size_t>(width_),
                                     static_cast<std::size_t>(height_), 4u, imageBytes)
                || !CheckedSizeMultiply(imageBytes, 2u, imageBytes)
                || !CheckedSizeAdd(chainBytes, imageBytes, retainedBytes)
                || retainedBytes > kSkiaCpuTextureStorageLimitBytes)
            {
                throw System::NotSupportedException(
                    "Skia Texture2D compressed mip storage and decoded image views exceed the "
                    "checked 256 MiB per-resource limit.");
            }

            compressedChain_ = std::make_unique<SkiaCompressedMipChain2D>(
                width_, height_, data.mipLevels > 1, blockByteSize, resourceCounters_);
            std::memcpy(compressedChain_->LevelData(0), data.pixels.data(), data.pixels.size());
            RebuildImage();
            if (resourceCounters_)
            {
                imageViewStorageBytes_ = imageBytes;
                resourceCounters_->AddTexture(imageViewStorageBytes_);
                resourceRegistered_ = true;
            }
            return;
        }
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

        const bool usesRgbaF32WorkingView =
            NeedsExpandedFloatImage(format_) || NeedsDecodedRgbaF32Image(format_);
        const std::size_t workingBytesPerTexel =
            usesRgbaF32WorkingView ? 16u
            : format_ == SurfaceFormat::Bgra4444 ? 4u
            : bytesPerTexel_;
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

        if (compressedChain_)
        {
            const SkiaCompressedMipLevel2D& level0 = compressedChain_->Level(0);
            if (stride != static_cast<int>(level0.rowBytes))
            {
                throw std::runtime_error(
                    "Skia Texture2D compressed UpdatePixels stride must equal the exact "
                    "padded block row size.");
            }
            std::memcpy(compressedChain_->LevelData(0), rgba, level0.bytes);
            RebuildImage();
            return;
        }

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

        if (compressedChain_)
        {
            const SkiaCompressedMipLevel2D& target = compressedChain_->Level(level);
            if (levelWidth != target.width || levelHeight != target.height)
            {
                throw std::runtime_error(
                    "Skia Texture2D compressed mip upload dimensions do not match the "
                    "requested level.");
            }
            if (level == 0)
            {
                UpdatePixels(rgba, static_cast<int>(target.rowBytes));
                return;
            }
            std::memcpy(compressedChain_->LevelData(level), rgba, target.bytes);
            return;
        }

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
        if (!data) return false;

        if (compressedChain_)
        {
            if (level < 0 || level >= compressedChain_->LevelCount()) return false;
            const SkiaCompressedMipLevel2D& sourceLevel = compressedChain_->Level(level);
            // A partial rect must start on a block boundary and either be block-aligned or
            // reach the level's actual (possibly NPOT) edge -- the same policy real block-
            // compression hardware/drivers enforce for sub-level updates.
            const bool touchesRightEdge = x + width == sourceLevel.width;
            const bool touchesBottomEdge = y + height == sourceLevel.height;
            if (width <= 0 || height <= 0 || x < 0 || y < 0
                || width > sourceLevel.width || height > sourceLevel.height
                || x > sourceLevel.width - width || y > sourceLevel.height - height
                || (x % 4) != 0 || (y % 4) != 0
                || ((width % 4) != 0 && !touchesRightEdge)
                || ((height % 4) != 0 && !touchesBottomEdge))
            {
                return false;
            }
            const std::size_t blockByteSize = CompressedBlockByteSize(format_);
            const int blockCols = (width + 3) / 4;
            const int blockRows = (height + 3) / 4;
            std::size_t requiredBytes = 0;
            if (!CheckedTexelBytes2D(static_cast<std::size_t>(blockCols),
                                     static_cast<std::size_t>(blockRows), blockByteSize,
                                     requiredBytes)
                || dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            {
                return false;
            }

            auto* destination = static_cast<std::uint8_t*>(data);
            const int blockX = x / 4;
            const int blockY = y / 4;
            const std::size_t rowCopyBytes = static_cast<std::size_t>(blockCols) * blockByteSize;
            for (int row = 0; row < blockRows; ++row)
            {
                const std::size_t sourceOffset =
                    static_cast<std::size_t>(blockY + row) * sourceLevel.rowBytes
                    + static_cast<std::size_t>(blockX) * blockByteSize;
                std::memcpy(destination + static_cast<std::size_t>(row) * rowCopyBytes,
                            compressedChain_->LevelData(level) + sourceOffset, rowCopyBytes);
            }
            return true;
        }

        if (level < 0 || level >= mipChain_->LevelCount())
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
        if (compressedChain_)
        {
            if (level < 0 || level >= compressedChain_->LevelCount())
                return nullptr;
            if (level == 0)
                return SnapshotImage(alphaConvention);

            const SkiaCompressedMipLevel2D& mip = compressedChain_->Level(level);
            const SkAlphaType alphaType =
                ResolveSkiaWorkingSourceRoute(StorageAlphaEXT(), alphaConvention)
                        == SkiaWorkingSourceRoute::PreserveDeclaredComponents
                    ? kPremul_SkAlphaType
                    : kUnpremul_SkAlphaType;
            // Decoded on demand, matching the other conversion-shadow formats below -- the
            // padded compressed block chain has stable addresses, but there is no direct Skia
            // representation for compressed bytes, so every snapshot decodes a fresh bounded
            // RGBA8 image.
            return MakeDecodedCompressedImage(format_, compressedChain_->LevelData(level),
                                              mip.bytes, mip.width, mip.height, alphaType);
        }

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
        // synchronous raster draw. Bgra4444 and expanded Single/Vector2 views take their
        // mandatory bounded conversion copy here.
        return MakePackedMipImage(format_, mipChain_->LevelData(level), mip.width, mip.height,
                                  mip.rowBytes, mip.bytes, alphaType);
    }

    void SkiaTextureBackend::RebuildImage()
    {
        if (compressedChain_)
        {
            const SkiaCompressedMipLevel2D& levelZero = compressedChain_->Level(0);
            straightImage_ = MakeDecodedCompressedImage(
                format_, compressedChain_->LevelData(0), levelZero.bytes, width_, height_,
                kUnpremul_SkAlphaType);
            premultipliedImage_ = MakeDecodedCompressedImage(
                format_, compressedChain_->LevelData(0), levelZero.bytes, width_, height_,
                kPremul_SkAlphaType);
            if (!straightImage_ || !premultipliedImage_)
                throw std::runtime_error("Skia failed to create a raster Texture2D image.");
            return;
        }

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
        if (compressedChain_)
        {
            // Compressed descendant levels are never generated -- see the class-level comment.
            if (level < 0 || level >= compressedChain_->LevelCount())
                throw std::out_of_range("Skia Texture2D mip generation level is out of range.");
            return 0u;
        }
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
        GenerateFormattedSkiaMipLevel(format_, bytesPerTexel_, *mipChain_, level);
    }
} // namespace CNA::Internal::Backends::Skia
