#include "CNA/Internal/Backends/Skia/SkiaMipGeneration.hpp"

#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace CNA::Internal::Backends::Skia
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    std::uint16_t ReadU16Le(const std::uint8_t* bytes) noexcept
    {
        return static_cast<std::uint16_t>(bytes[0])
            | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
    }

    std::uint32_t ReadU32Le(const std::uint8_t* bytes) noexcept
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

    int DecodeSignedByte(std::uint8_t value) noexcept
    {
        return value < 0x80u ? static_cast<int>(value) : static_cast<int>(value) - 256;
    }

    int AverageSnormComponent(std::int64_t sum, std::uint32_t sampleCount) noexcept
    {
        const std::int64_t half = static_cast<std::int64_t>(sampleCount / 2u);
        return static_cast<int>(sum >= 0
            ? (sum + half) / static_cast<std::int64_t>(sampleCount)
            : (sum - half) / static_cast<std::int64_t>(sampleCount));
    }

    bool IsSnormTextureFormat(SurfaceFormat format) noexcept
    {
        return format == SurfaceFormat::NormalizedByte2 || format == SurfaceFormat::NormalizedByte4;
    }

    int SnormChannelCount(SurfaceFormat format) noexcept
    {
        return format == SurfaceFormat::NormalizedByte2 ? 2 : 4;
    }

    double SrgbByteToLinear(std::uint8_t value) noexcept
    {
        const double encoded = static_cast<double>(value) / 255.0;
        return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
    }

    std::uint8_t LinearToSrgbByte(double value) noexcept
    {
        const double linear = std::clamp(value, 0.0, 1.0);
        const double encoded = linear <= 0.0031308
            ? linear * 12.92
            : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        return static_cast<std::uint8_t>(std::clamp(std::lround(encoded * 255.0), 0l, 255l));
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
                const std::uint64_t sampleCount = static_cast<std::uint64_t>((x1 - x0) * (y1 - y0));
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
                output[3] = static_cast<std::uint8_t>((alphaSum + sampleCount / 2u) / sampleCount);
            }
        }
    }

    bool IsFloatTextureFormat(SurfaceFormat format) noexcept
    {
        return format == SurfaceFormat::Single
            || format == SurfaceFormat::Vector2
            || format == SurfaceFormat::Vector4
            || format == SurfaceFormat::HalfSingle
            || format == SurfaceFormat::HalfVector2
            || format == SurfaceFormat::HalfVector4
            || format == SurfaceFormat::HdrBlendable;
    }

    bool IsHalfTextureFormat(SurfaceFormat format) noexcept
    {
        return format == SurfaceFormat::HalfSingle
            || format == SurfaceFormat::HalfVector2
            || format == SurfaceFormat::HalfVector4
            || format == SurfaceFormat::HdrBlendable;
    }

    int FloatChannelCount(SurfaceFormat format) noexcept
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

    float ReadFloatChannel(SurfaceFormat format, const std::uint8_t* texel, int channel) noexcept
    {
        if (IsHalfTextureFormat(format))
        {
            return Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper::Convert(
                ReadU16Le(texel + static_cast<std::size_t>(channel) * 2u));
        }
        return std::bit_cast<float>(ReadU32Le(texel + static_cast<std::size_t>(channel) * 4u));
    }

    void WriteFloatChannel(SurfaceFormat format, std::uint8_t* texel, int channel,
                           float value) noexcept
    {
        if (IsHalfTextureFormat(format))
        {
            const std::uint16_t bits = std::isnan(value)
                ? static_cast<std::uint16_t>(0x7E00u)
                : Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper::Convert(value);
            WriteU16Le(texel + static_cast<std::size_t>(channel) * 2u, bits);
            return;
        }
        const std::uint32_t bits = std::isnan(value) ? 0x7FC00000u : std::bit_cast<std::uint32_t>(value);
        WriteU32Le(texel + static_cast<std::size_t>(channel) * 4u, bits);
    }

    void FloatMipAccumulator::Add(float value) noexcept
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

    float FloatMipAccumulator::Average(std::uint32_t sampleCount) const noexcept
    {
        // Generated mips use a stable IEEE policy independent of source NaN payload: any NaN, or
        // opposing infinities, becomes canonical positive quiet NaN; one infinity sign dominates
        // finite samples; finite samples average in double.
        if (hasNaN || (hasPositiveInfinity && hasNegativeInfinity))
            return std::bit_cast<float>(0x7FC00000u);
        if (hasPositiveInfinity) return std::numeric_limits<float>::infinity();
        if (hasNegativeInfinity) return -std::numeric_limits<float>::infinity();
        return static_cast<float>(finiteSum / static_cast<double>(sampleCount));
    }

    void GenerateFormattedSkiaMipLevel(SurfaceFormat format, std::size_t bytesPerTexel,
                                       SkiaMipChain2D& chain, int level)
    {
        if (format == SurfaceFormat::Color || format == SurfaceFormat::ColorBgraEXT)
        {
            GenerateSkiaRgba8MipLevel(chain, level);
            return;
        }
        if (format == SurfaceFormat::ColorSrgbEXT)
        {
            GenerateSrgbMipLevel(chain, level);
            return;
        }

        if (IsSnormTextureFormat(format))
        {
            const SkiaMipLevel2D& source = chain.Level(level - 1);
            const SkiaMipLevel2D& target = chain.Level(level);
            const std::uint8_t* sourcePixels = chain.LevelData(level - 1);
            std::uint8_t* targetPixels = chain.LevelData(level);
            const int channelCount = SnormChannelCount(format);
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
                                + static_cast<std::size_t>(sourceX) * bytesPerTexel;
                            for (int channel = 0; channel < channelCount; ++channel)
                            {
                                sums[channel] += std::max(-127, DecodeSignedByte(texel[channel]));
                            }
                        }
                    }
                    std::uint8_t* output = targetPixels
                        + static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * bytesPerTexel;
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        output[channel] = static_cast<std::uint8_t>(
                            AverageSnormComponent(sums[channel], sampleCount));
                    }
                }
            }
            return;
        }

        if (IsFloatTextureFormat(format))
        {
            const SkiaMipLevel2D& source = chain.Level(level - 1);
            const SkiaMipLevel2D& target = chain.Level(level);
            const std::uint8_t* sourcePixels = chain.LevelData(level - 1);
            std::uint8_t* targetPixels = chain.LevelData(level);
            const int channelCount = FloatChannelCount(format);
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
                                + static_cast<std::size_t>(sourceX) * bytesPerTexel;
                            for (int channel = 0; channel < channelCount; ++channel)
                                accumulators[channel].Add(ReadFloatChannel(format, texel, channel));
                        }
                    }
                    std::uint8_t* output = targetPixels
                        + static_cast<std::size_t>(y) * target.rowBytes
                        + static_cast<std::size_t>(x) * bytesPerTexel;
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        WriteFloatChannel(format, output, channel,
                                          accumulators[channel].Average(sampleCount));
                    }
                }
            }
            return;
        }

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
                const std::uint32_t sampleCount = static_cast<std::uint32_t>((x1 - x0) * (y1 - y0));
                std::uint64_t sums[4] = {0u, 0u, 0u, 0u};
                for (int sourceY = y0; sourceY < y1; ++sourceY)
                {
                    for (int sourceX = x0; sourceX < x1; ++sourceX)
                    {
                        const std::uint8_t* texel = sourcePixels
                            + static_cast<std::size_t>(sourceY) * source.rowBytes
                            + static_cast<std::size_t>(sourceX) * bytesPerTexel;
                        if (format == SurfaceFormat::Bgr565)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 11u) & 0x1Fu;
                            sums[1] += (word >> 5u) & 0x3Fu;
                            sums[2] += word & 0x1Fu;
                        }
                        else if (format == SurfaceFormat::Bgra4444)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 8u) & 0xFu;
                            sums[1] += (word >> 4u) & 0xFu;
                            sums[2] += word & 0xFu;
                            sums[3] += (word >> 12u) & 0xFu;
                        }
                        else if (format == SurfaceFormat::Bgra5551)
                        {
                            const std::uint16_t word = ReadU16Le(texel);
                            sums[0] += (word >> 10u) & 0x1Fu;
                            sums[1] += (word >> 5u) & 0x1Fu;
                            sums[2] += word & 0x1Fu;
                            sums[3] += (word >> 15u) & 0x1u;
                        }
                        else if (format == SurfaceFormat::Alpha8 || format == SurfaceFormat::ByteEXT)
                        {
                            sums[0] += texel[0];
                        }
                        else if (format == SurfaceFormat::UShortEXT)
                        {
                            sums[0] += ReadU16Le(texel);
                        }
                        else if (format == SurfaceFormat::Rg32)
                        {
                            sums[0] += ReadU16Le(texel);
                            sums[1] += ReadU16Le(texel + 2u);
                        }
                        else if (format == SurfaceFormat::Rgba64)
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
                    return static_cast<std::uint32_t>((sum + sampleCount / 2u) / sampleCount);
                };
                std::uint8_t* output = targetPixels
                    + static_cast<std::size_t>(y) * target.rowBytes
                    + static_cast<std::size_t>(x) * bytesPerTexel;
                if (format == SurfaceFormat::Bgr565)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[0]) << 11u) | (average(sums[1]) << 5u) | average(sums[2])));
                }
                else if (format == SurfaceFormat::Bgra4444)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[3]) << 12u) | (average(sums[0]) << 8u)
                        | (average(sums[1]) << 4u) | average(sums[2])));
                }
                else if (format == SurfaceFormat::Bgra5551)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(
                        (average(sums[3]) << 15u) | (average(sums[0]) << 10u)
                        | (average(sums[1]) << 5u) | average(sums[2])));
                }
                else if (format == SurfaceFormat::Alpha8 || format == SurfaceFormat::ByteEXT)
                {
                    output[0] = static_cast<std::uint8_t>(average(sums[0]));
                }
                else if (format == SurfaceFormat::UShortEXT)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                }
                else if (format == SurfaceFormat::Rg32)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                    WriteU16Le(output + 2u, static_cast<std::uint16_t>(average(sums[1])));
                }
                else if (format == SurfaceFormat::Rgba64)
                {
                    WriteU16Le(output, static_cast<std::uint16_t>(average(sums[0])));
                    WriteU16Le(output + 2u, static_cast<std::uint16_t>(average(sums[1])));
                    WriteU16Le(output + 4u, static_cast<std::uint16_t>(average(sums[2])));
                    WriteU16Le(output + 6u, static_cast<std::uint16_t>(average(sums[3])));
                }
                else
                {
                    WriteU32Le(output, average(sums[0]) | (average(sums[1]) << 10u)
                        | (average(sums[2]) << 20u) | (average(sums[3]) << 30u));
                }
            }
        }
    }
} // namespace CNA::Internal::Backends::Skia
