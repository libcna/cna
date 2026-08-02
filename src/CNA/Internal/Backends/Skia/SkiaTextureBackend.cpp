#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaResourcePolicy.hpp"

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
                || format == SurfaceFormat::HdrBlendable;
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

        [[nodiscard]] bool NeedsDecodedRgbaF32Image(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Bgra5551
                || format == SurfaceFormat::NormalizedByte2
                || format == SurfaceFormat::NormalizedByte4;
        }

        [[nodiscard]] int DecodeSignedByte(std::uint8_t value) noexcept
        {
            return value < 0x80u ? static_cast<int>(value) : static_cast<int>(value) - 256;
        }

        [[nodiscard]] float DecodeSnormByte(std::uint8_t value) noexcept
        {
            return static_cast<float>(std::max(-127, DecodeSignedByte(value))) / 127.0f;
        }

        [[nodiscard]] int AverageSnormComponent(
            std::int64_t sum, std::uint32_t sampleCount) noexcept
        {
            const std::int64_t half = static_cast<std::int64_t>(sampleCount / 2u);
            return static_cast<int>(sum >= 0
                ? (sum + half) / static_cast<std::int64_t>(sampleCount)
                : (sum - half) / static_cast<std::int64_t>(sampleCount));
        }

        [[nodiscard]] bool IsSnormTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::NormalizedByte2
                || format == SurfaceFormat::NormalizedByte4;
        }

        [[nodiscard]] int SnormChannelCount(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::NormalizedByte2 ? 2 : 4;
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

        [[nodiscard]] double SrgbByteToLinear(std::uint8_t value) noexcept
        {
            const double encoded = static_cast<double>(value) / 255.0;
            return encoded <= 0.04045
                ? encoded / 12.92
                : std::pow((encoded + 0.055) / 1.055, 2.4);
        }

        [[nodiscard]] std::uint8_t LinearToSrgbByte(double value) noexcept
        {
            const double linear = std::clamp(value, 0.0, 1.0);
            const double encoded = linear <= 0.0031308
                ? linear * 12.92
                : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            return static_cast<std::uint8_t>(
                std::clamp(std::lround(encoded * 255.0), 0l, 255l));
        }

        void GenerateSrgbMipLevel(SkiaMipChain2D& chain, int level)
        {
            const SkiaMipLevel2D& source = chain.Level(level - 1);
            const SkiaMipLevel2D& target = chain.Level(level);
            const std::uint8_t* sourcePixels = chain.LevelData(level - 1);
            std::uint8_t* targetPixels = chain.LevelData(level);
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
                    const std::uint64_t sampleCount = static_cast<std::uint64_t>(
                        (x1 - x0) * (y1 - y0));
                    double linearSums[3] = {0.0, 0.0, 0.0};
                    std::uint64_t alphaSum = 0u;
                    for (int sourceY = y0; sourceY < y1; ++sourceY)
                    {
                        for (int sourceX = x0; sourceX < x1; ++sourceX)
                        {
                            const std::uint8_t* texel = sourcePixels
                                + static_cast<std::size_t>(sourceY) * source.rowBytes
                                + static_cast<std::size_t>(sourceX) * 4u;
                            linearSums[0] += SrgbByteToLinear(texel[0]);
                            linearSums[1] += SrgbByteToLinear(texel[1]);
                            linearSums[2] += SrgbByteToLinear(texel[2]);
                            alphaSum += texel[3];
                        }
                    }
                    std::uint8_t* output = targetPixels
                        + static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * 4u;
                    output[0] = LinearToSrgbByte(linearSums[0] / sampleCount);
                    output[1] = LinearToSrgbByte(linearSums[1] / sampleCount);
                    output[2] = LinearToSrgbByte(linearSums[2] / sampleCount);
                    output[3] = static_cast<std::uint8_t>(
                        (alphaSum + sampleCount / 2u) / sampleCount);
                }
            }
        }

        [[nodiscard]] bool IsFloatTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::Single
                || format == SurfaceFormat::Vector2
                || format == SurfaceFormat::Vector4
                || format == SurfaceFormat::HalfSingle
                || format == SurfaceFormat::HalfVector2
                || format == SurfaceFormat::HalfVector4
                || format == SurfaceFormat::HdrBlendable;
        }

        [[nodiscard]] bool IsHalfTextureFormat(SurfaceFormat format) noexcept
        {
            return format == SurfaceFormat::HalfSingle
                || format == SurfaceFormat::HalfVector2
                || format == SurfaceFormat::HalfVector4
                || format == SurfaceFormat::HdrBlendable;
        }

        [[nodiscard]] int FloatChannelCount(SurfaceFormat format) noexcept
        {
            switch (format)
            {
                case SurfaceFormat::Single:
                case SurfaceFormat::HalfSingle:
                    return 1;
                case SurfaceFormat::Vector2:
                case SurfaceFormat::HalfVector2:
                    return 2;
                default:
                    return 4;
            }
        }

        [[nodiscard]] float ReadFloatChannel(
            SurfaceFormat format, const std::uint8_t* texel, int channel) noexcept
        {
            if (IsHalfTextureFormat(format))
            {
                return Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper::Convert(
                    ReadU16Le(texel + static_cast<std::size_t>(channel) * 2u));
            }
            return std::bit_cast<float>(
                ReadU32Le(texel + static_cast<std::size_t>(channel) * 4u));
        }

        void WriteFloatChannel(SurfaceFormat format, std::uint8_t* texel,
                               int channel, float value) noexcept
        {
            if (IsHalfTextureFormat(format))
            {
                const std::uint16_t bits = std::isnan(value)
                    ? static_cast<std::uint16_t>(0x7E00u)
                    : Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper::Convert(
                        value);
                WriteU16Le(texel + static_cast<std::size_t>(channel) * 2u, bits);
                return;
            }
            const std::uint32_t bits = std::isnan(value)
                ? 0x7FC00000u
                : std::bit_cast<std::uint32_t>(value);
            WriteU32Le(texel + static_cast<std::size_t>(channel) * 4u, bits);
        }

        struct FloatMipAccumulator final
        {
            double finiteSum = 0.0;
            bool hasFinite = false;
            bool hasNaN = false;
            bool hasPositiveInfinity = false;
            bool hasNegativeInfinity = false;

            void Add(float value) noexcept
            {
                if (std::isnan(value))
                {
                    hasNaN = true;
                    return;
                }
                if (std::isinf(value))
                {
                    if (std::signbit(value)) hasNegativeInfinity = true;
                    else hasPositiveInfinity = true;
                    return;
                }
                if (!hasFinite)
                {
                    finiteSum = value;
                    hasFinite = true;
                }
                else
                {
                    finiteSum += value;
                }
            }

            [[nodiscard]] float Average(std::uint32_t sampleCount) const noexcept
            {
                // Generated mips use a stable IEEE policy independent of source NaN payload:
                // any NaN, or opposing infinities, becomes canonical positive quiet NaN;
                // one infinity sign dominates finite samples; finite samples average in double.
                if (hasNaN || (hasPositiveInfinity && hasNegativeInfinity))
                    return std::bit_cast<float>(0x7FC00000u);
                if (hasPositiveInfinity)
                    return std::numeric_limits<float>::infinity();
                if (hasNegativeInfinity)
                    return -std::numeric_limits<float>::infinity();
                return static_cast<float>(finiteSum / static_cast<double>(sampleCount));
            }
        };
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
        // synchronous raster draw. Bgra4444 and expanded Single/Vector2 views take their
        // mandatory bounded conversion copy here.
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
        if (format_ == SurfaceFormat::Color || format_ == SurfaceFormat::ColorBgraEXT)
        {
            GenerateSkiaRgba8MipLevel(*mipChain_, level);
            return;
        }
        if (format_ == SurfaceFormat::ColorSrgbEXT)
        {
            GenerateSrgbMipLevel(*mipChain_, level);
            return;
        }

        if (IsSnormTextureFormat(format_))
        {
            const SkiaMipLevel2D& source = mipChain_->Level(level - 1);
            const SkiaMipLevel2D& target = mipChain_->Level(level);
            const std::uint8_t* sourcePixels = mipChain_->LevelData(level - 1);
            std::uint8_t* targetPixels = mipChain_->LevelData(level);
            const int channelCount = SnormChannelCount(format_);
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
                    std::int64_t sums[4] = {0, 0, 0, 0};
                    for (int sourceY = y0; sourceY < y1; ++sourceY)
                    {
                        for (int sourceX = x0; sourceX < x1; ++sourceX)
                        {
                            const std::uint8_t* texel = sourcePixels
                                + static_cast<std::size_t>(sourceY) * source.rowBytes
                                + static_cast<std::size_t>(sourceX) * bytesPerTexel_;
                            for (int channel = 0; channel < channelCount; ++channel)
                            {
                                sums[channel] += std::max(
                                    -127, DecodeSignedByte(texel[channel]));
                            }
                        }
                    }
                    std::uint8_t* output = targetPixels
                        + static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * bytesPerTexel_;
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        output[channel] = static_cast<std::uint8_t>(
                            AverageSnormComponent(sums[channel], sampleCount));
                    }
                }
            }
            return;
        }

        if (IsFloatTextureFormat(format_))
        {
            const SkiaMipLevel2D& source = mipChain_->Level(level - 1);
            const SkiaMipLevel2D& target = mipChain_->Level(level);
            const std::uint8_t* sourcePixels = mipChain_->LevelData(level - 1);
            std::uint8_t* targetPixels = mipChain_->LevelData(level);
            const int channelCount = FloatChannelCount(format_);
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
                    FloatMipAccumulator accumulators[4];
                    for (int sourceY = y0; sourceY < y1; ++sourceY)
                    {
                        for (int sourceX = x0; sourceX < x1; ++sourceX)
                        {
                            const std::uint8_t* texel = sourcePixels
                                + static_cast<std::size_t>(sourceY) * source.rowBytes
                                + static_cast<std::size_t>(sourceX) * bytesPerTexel_;
                            for (int channel = 0; channel < channelCount; ++channel)
                                accumulators[channel].Add(
                                    ReadFloatChannel(format_, texel, channel));
                        }
                    }
                    std::uint8_t* output = targetPixels
                        + static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * bytesPerTexel_;
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        WriteFloatChannel(format_, output, channel,
                                          accumulators[channel].Average(sampleCount));
                    }
                }
            }
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
                        else if (format_ == SurfaceFormat::Bgra5551)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 10u) & 0x1Fu;
                            sums[1] += (word >> 5u) & 0x1Fu;
                            sums[2] += word & 0x1Fu;
                            sums[3] += (word >> 15u) & 0x1u;
                        }
                        else if (format_ == SurfaceFormat::Alpha8
                                 || format_ == SurfaceFormat::ByteEXT)
                        {
                            sums[0] += texel[0];
                        }
                        else if (format_ == SurfaceFormat::UShortEXT)
                        {
                            sums[0] += ReadU16Le(texel);
                        }
                        else if (format_ == SurfaceFormat::Rg32)
                        {
                            sums[0] += ReadU16Le(texel);
                            sums[1] += ReadU16Le(texel + 2u);
                        }
                        else if (format_ == SurfaceFormat::Rgba64)
                        {
                            sums[0] += ReadU16Le(texel);
                            sums[1] += ReadU16Le(texel + 2u);
                            sums[2] += ReadU16Le(texel + 4u);
                            sums[3] += ReadU16Le(texel + 6u);
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
                else if (format_ == SurfaceFormat::Bgra5551)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[3]) << 15u) | (average(sums[0]) << 10u)
                        | (average(sums[1]) << 5u) | average(sums[2])));
                }
                else if (format_ == SurfaceFormat::Alpha8
                         || format_ == SurfaceFormat::ByteEXT)
                {
                    output[0] = static_cast<std::uint8_t>(average(sums[0]));
                }
                else if (format_ == SurfaceFormat::UShortEXT)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                }
                else if (format_ == SurfaceFormat::Rg32)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                    WriteU16Le(output + 2u, static_cast<std::uint16_t>(average(sums[1])));
                }
                else if (format_ == SurfaceFormat::Rgba64)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                    WriteU16Le(output + 2u, static_cast<std::uint16_t>(average(sums[1])));
                    WriteU16Le(output + 4u, static_cast<std::uint16_t>(average(sums[2])));
                    WriteU16Le(output + 6u, static_cast<std::uint16_t>(average(sums[3])));
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
