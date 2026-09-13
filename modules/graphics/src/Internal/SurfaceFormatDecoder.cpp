// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Graphics/SurfaceFormatDecoder.hpp"

#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace CNA::Internal::Graphics::SurfaceFormatDecoder
{
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    bool IsDxt(int surfaceFormat) noexcept
    {
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
               format == SurfaceFormat::Dxt5;
    }

    namespace
    {
        [[nodiscard]] int DxtBlockBytes(int surfaceFormat)
        {
            return static_cast<SurfaceFormat>(surfaceFormat) == SurfaceFormat::Dxt1 ? 8 : 16;
        }

        [[nodiscard]] std::uint16_t Read16(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint16_t>(bytes[0]) |
                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
        }

        [[nodiscard]] std::uint32_t Read32(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0]) |
                   (static_cast<std::uint32_t>(bytes[1]) << 8u) |
                   (static_cast<std::uint32_t>(bytes[2]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[3]) << 24u);
        }

        [[nodiscard]] std::uint64_t Read64(const std::uint8_t* bytes) noexcept
        {
            return static_cast<std::uint64_t>(Read32(bytes)) |
                   (static_cast<std::uint64_t>(Read32(bytes + 4)) << 32u);
        }

        [[nodiscard]] float ReadSingle(const std::uint8_t* bytes) noexcept
        {
            return std::bit_cast<float>(Read32(bytes));
        }

        [[nodiscard]] float DecodeSnorm8(std::uint8_t bits) noexcept
        {
            const int value = bits <= 127u ? static_cast<int>(bits)
                                           : static_cast<int>(bits) - 256;
            return value <= -127 ? -1.0f : static_cast<float>(value) / 127.0f;
        }

        [[nodiscard]] std::uint8_t FloatToByte(float value) noexcept
        {
            if (!(value > 0.0f)) return 0u;
            if (value >= 1.0f) return 255u;
            return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
        }

        void StoreDecodedTexel(std::size_t texel, float r, float g, float b, float a,
                               std::vector<std::uint8_t>& rgba8,
                               std::vector<float>& samples)
        {
            const std::size_t offset = texel * 4u;
            rgba8[offset + 0] = FloatToByte(r);
            rgba8[offset + 1] = FloatToByte(g);
            rgba8[offset + 2] = FloatToByte(b);
            rgba8[offset + 3] = FloatToByte(a);
            samples[offset + 0] = r;
            samples[offset + 1] = g;
            samples[offset + 2] = b;
            samples[offset + 3] = a;
        }
    }

    int BytesPerTexel(int surfaceFormat)
    {
        return Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(
            static_cast<SurfaceFormat>(surfaceFormat));
    }

    std::size_t RawByteCount(int surfaceFormat, int width, int height)
    {
        if (IsDxt(surfaceFormat))
        {
            return static_cast<std::size_t>((width + 3) / 4) *
                   static_cast<std::size_t>((height + 3) / 4) *
                   static_cast<std::size_t>(DxtBlockBytes(surfaceFormat));
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
               static_cast<std::size_t>(BytesPerTexel(surfaceFormat));
    }

    void DecodePixels(int surfaceFormat, const std::uint8_t* source, std::size_t sourceBytes,
                      int sourceStride, int width, int height,
                      std::vector<std::uint8_t>& destination,
                      std::vector<float>& samples,
                      MissingColorChannels missingChannels)
    {
        const std::size_t requiredBytes = RawByteCount(surfaceFormat, width, height);
        if (source == nullptr || sourceBytes < requiredBytes)
            throw std::invalid_argument("SurfaceFormatDecoder: source storage is incomplete");

        if (IsDxt(surfaceFormat))
        {
            using CNA::Internal::Graphics::DxtUtil;
            const auto format = static_cast<SurfaceFormat>(surfaceFormat);
            destination = format == SurfaceFormat::Dxt1
                ? DxtUtil::DecompressDxt1(source, sourceBytes, width, height)
                : (format == SurfaceFormat::Dxt3
                    ? DxtUtil::DecompressDxt3(source, sourceBytes, width, height)
                    : DxtUtil::DecompressDxt5(source, sourceBytes, width, height));
            samples.resize(destination.size());
            for (std::size_t i = 0; i < destination.size(); ++i)
                samples[i] = destination[i] / 255.0f;
            return;
        }

        const int bytesPerTexel = BytesPerTexel(surfaceFormat);
        if (sourceStride < width * bytesPerTexel ||
            static_cast<std::size_t>(sourceStride) * static_cast<std::size_t>(height) > sourceBytes)
        {
            throw std::invalid_argument("SurfaceFormatDecoder: source row pitch is invalid");
        }

        destination.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        samples.resize(destination.size());
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        const float missing = missingChannels == MissingColorChannels::TextureSampling ? 1.0f : 0.0f;
        using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;
        for (int y = 0; y < height; ++y)
        {
            const std::uint8_t* row = source + static_cast<std::size_t>(y) * sourceStride;
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t* texelBytes = row +
                    static_cast<std::size_t>(x) * static_cast<std::size_t>(bytesPerTexel);
                const std::size_t texel = static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                float a = 1.0f;
                switch (format)
                {
                case SurfaceFormat::Color:
                    r = texelBytes[0] / 255.0f;
                    g = texelBytes[1] / 255.0f;
                    b = texelBytes[2] / 255.0f;
                    a = texelBytes[3] / 255.0f;
                    break;
                case SurfaceFormat::Bgr565:
                {
                    const std::uint16_t packed = Read16(texelBytes);
                    r = static_cast<float>((packed >> 11) & 31u) / 31.0f;
                    g = static_cast<float>((packed >> 5) & 63u) / 63.0f;
                    b = static_cast<float>(packed & 31u) / 31.0f;
                    break;
                }
                case SurfaceFormat::Bgra5551:
                {
                    const std::uint16_t packed = Read16(texelBytes);
                    r = static_cast<float>((packed >> 10) & 31u) / 31.0f;
                    g = static_cast<float>((packed >> 5) & 31u) / 31.0f;
                    b = static_cast<float>(packed & 31u) / 31.0f;
                    a = (packed & 0x8000u) != 0u ? 1.0f : 0.0f;
                    break;
                }
                case SurfaceFormat::Bgra4444:
                {
                    const std::uint16_t packed = Read16(texelBytes);
                    r = static_cast<float>((packed >> 8) & 15u) / 15.0f;
                    g = static_cast<float>((packed >> 4) & 15u) / 15.0f;
                    b = static_cast<float>(packed & 15u) / 15.0f;
                    a = static_cast<float>((packed >> 12) & 15u) / 15.0f;
                    break;
                }
                case SurfaceFormat::NormalizedByte2:
                    r = DecodeSnorm8(texelBytes[0]);
                    g = DecodeSnorm8(texelBytes[1]);
                    b = missing;
                    break;
                case SurfaceFormat::NormalizedByte4:
                    r = DecodeSnorm8(texelBytes[0]);
                    g = DecodeSnorm8(texelBytes[1]);
                    b = DecodeSnorm8(texelBytes[2]);
                    a = DecodeSnorm8(texelBytes[3]);
                    break;
                case SurfaceFormat::Rgba1010102:
                {
                    const std::uint32_t packed = Read32(texelBytes);
                    r = static_cast<float>(packed & 1023u) / 1023.0f;
                    g = static_cast<float>((packed >> 10) & 1023u) / 1023.0f;
                    b = static_cast<float>((packed >> 20) & 1023u) / 1023.0f;
                    a = static_cast<float>((packed >> 30) & 3u) / 3.0f;
                    break;
                }
                case SurfaceFormat::Rg32:
                {
                    const std::uint32_t packed = Read32(texelBytes);
                    r = static_cast<float>(packed & 65535u) / 65535.0f;
                    g = static_cast<float>(packed >> 16) / 65535.0f;
                    b = missing;
                    break;
                }
                case SurfaceFormat::Rgba64:
                {
                    const std::uint64_t packed = Read64(texelBytes);
                    r = static_cast<float>(packed & 65535u) / 65535.0f;
                    g = static_cast<float>((packed >> 16) & 65535u) / 65535.0f;
                    b = static_cast<float>((packed >> 32) & 65535u) / 65535.0f;
                    a = static_cast<float>((packed >> 48) & 65535u) / 65535.0f;
                    break;
                }
                case SurfaceFormat::Alpha8:
                    a = texelBytes[0] / 255.0f;
                    break;
                case SurfaceFormat::Single:
                    r = ReadSingle(texelBytes);
                    g = b = missing;
                    break;
                case SurfaceFormat::Vector2:
                    r = ReadSingle(texelBytes);
                    g = ReadSingle(texelBytes + 4);
                    b = missing;
                    break;
                case SurfaceFormat::Vector4:
                    r = ReadSingle(texelBytes);
                    g = ReadSingle(texelBytes + 4);
                    b = ReadSingle(texelBytes + 8);
                    a = ReadSingle(texelBytes + 12);
                    break;
                case SurfaceFormat::HalfSingle:
                    r = HalfTypeHelper::Convert(Read16(texelBytes));
                    g = b = missing;
                    break;
                case SurfaceFormat::HalfVector2:
                    r = HalfTypeHelper::Convert(Read16(texelBytes));
                    g = HalfTypeHelper::Convert(Read16(texelBytes + 2));
                    b = missing;
                    break;
                case SurfaceFormat::HalfVector4:
                case SurfaceFormat::HdrBlendable:
                    r = HalfTypeHelper::Convert(Read16(texelBytes));
                    g = HalfTypeHelper::Convert(Read16(texelBytes + 2));
                    b = HalfTypeHelper::Convert(Read16(texelBytes + 4));
                    a = HalfTypeHelper::Convert(Read16(texelBytes + 6));
                    break;
                default:
                    throw std::runtime_error("SurfaceFormatDecoder: unsupported classic SurfaceFormat");
                }
                StoreDecodedTexel(texel, r, g, b, a, destination, samples);
            }
        }
    }
}
