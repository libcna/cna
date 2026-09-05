// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/DdsSurfaceReader.hpp"

#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::Graphics
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x20534444u;
        constexpr std::uint32_t kHeaderSize = 124u;
        constexpr std::uint32_t kPixelFormatSize = 32u;
        constexpr std::uint32_t kCaps2Cubemap = 0x200u;
        constexpr std::uint32_t kCaps2Volume = 0x200000u;
        constexpr std::uint32_t kPixelFormatFourCc = 0x4u;
        constexpr std::uint32_t kPixelFormatRgb = 0x40u;
        constexpr std::uint32_t kPixelFormatAlphaPixels = 0x1u;
        constexpr std::uint32_t kFourCcDxt1 = 0x31545844u;
        constexpr std::uint32_t kFourCcDxt3 = 0x33545844u;
        constexpr std::uint32_t kFourCcDxt5 = 0x35545844u;
        constexpr std::uint32_t kFourCcDx10 = 0x30315844u;
        /** @brief The largest dimension a level may declare, which bounds every product below. */
        constexpr std::uint32_t kMaxDimension = 16384u;

        [[nodiscard]] std::uint32_t ReadUInt32(const std::span<const std::uint8_t> bytes, const std::size_t at)
        {
            return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
                   (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
        }

        /** @brief The bytes one level of one surface occupies. */
        [[nodiscard]] std::uint64_t LevelBytes(const std::uint64_t width, const std::uint64_t height,
                                               const DdsSurfaceFormat format, const std::uint32_t bitCount)
        {
            switch (format)
            {
                case DdsSurfaceFormat::Dxt1: return ((width + 3u) / 4u) * ((height + 3u) / 4u) * 8u;
                case DdsSurfaceFormat::Dxt3:
                case DdsSurfaceFormat::Dxt5: return ((width + 3u) / 4u) * ((height + 3u) / 4u) * 16u;
                case DdsSurfaceFormat::Color: break;
            }
            return width * height * (bitCount / 8u);
        }

        /** @brief The shift and width of a channel mask, for the conversion to RGBA. */
        struct Channel
        {
            std::uint32_t mask = 0u;
            std::uint32_t shift = 0u;
            std::uint32_t bits = 0u;
        };

        [[nodiscard]] Channel Describe(const std::uint32_t mask)
        {
            Channel channel;
            channel.mask = mask;
            if (mask == 0u)
            {
                return channel;
            }
            while (((mask >> channel.shift) & 1u) == 0u)
            {
                ++channel.shift;
            }
            std::uint32_t value = mask >> channel.shift;
            while ((value & 1u) != 0u)
            {
                ++channel.bits;
                value >>= 1;
            }
            return channel;
        }

        [[nodiscard]] std::uint8_t Extract(const std::uint32_t pixel, const Channel& channel,
                                           const std::uint8_t whenAbsent)
        {
            if (channel.mask == 0u || channel.bits == 0u)
            {
                return whenAbsent;
            }
            const std::uint32_t value = (pixel & channel.mask) >> channel.shift;
            const std::uint32_t maximum = (1u << channel.bits) - 1u;
            return static_cast<std::uint8_t>(maximum == 0u ? 0u : value * 255u / maximum);
        }
    }

    bool IsDds(const std::span<const std::uint8_t> bytes) noexcept
    {
        return bytes.size() >= 4u && ReadUInt32(bytes, 0) == kMagic;
    }

    DdsSurfaces ReadDdsSurfaces(const std::span<const std::uint8_t> bytes, const std::string& origin)
    {
        const auto refuse = [&origin](const char* reason)
        { throw std::runtime_error("'" + origin + "': " + reason); };
        if (bytes.size() < 128u || !IsDds(bytes))
        {
            refuse("not a DDS file.");
        }
        if (ReadUInt32(bytes, 4) != kHeaderSize || ReadUInt32(bytes, 76) != kPixelFormatSize)
        {
            refuse("the DDS header is not the one this reader understands.");
        }
        DdsSurfaces result;
        result.height = ReadUInt32(bytes, 12);
        result.width = ReadUInt32(bytes, 16);
        const std::uint32_t depth = ReadUInt32(bytes, 24);
        result.mipCount = std::max<std::uint32_t>(1u, ReadUInt32(bytes, 28));
        const std::uint32_t pixelFormatFlags = ReadUInt32(bytes, 80);
        const std::uint32_t fourCc = ReadUInt32(bytes, 84);
        const std::uint32_t bitCount = ReadUInt32(bytes, 88);
        const Channel red = Describe(ReadUInt32(bytes, 92));
        const Channel green = Describe(ReadUInt32(bytes, 96));
        const Channel blue = Describe(ReadUInt32(bytes, 100));
        const Channel alpha = Describe(ReadUInt32(bytes, 104));
        const std::uint32_t caps2 = ReadUInt32(bytes, 112);
        result.isCube = (caps2 & kCaps2Cubemap) != 0u;
        result.isVolume = (caps2 & kCaps2Volume) != 0u;
        result.depth = result.isVolume ? std::max<std::uint32_t>(1u, depth) : 1u;
        if (result.width == 0u || result.height == 0u || result.width > kMaxDimension ||
            result.height > kMaxDimension || result.depth > kMaxDimension)
        {
            refuse("the DDS header names a size this reader will not read.");
        }
        if ((pixelFormatFlags & kPixelFormatFourCc) != 0u)
        {
            switch (fourCc)
            {
                case kFourCcDxt1: result.format = DdsSurfaceFormat::Dxt1; break;
                case kFourCcDxt3: result.format = DdsSurfaceFormat::Dxt3; break;
                case kFourCcDxt5: result.format = DdsSurfaceFormat::Dxt5; break;
                case kFourCcDx10:
                    // The runtime this mirrors refuses a DX10 extension outright, and so does this
                    // reader rather than guessing what its own consumers would do with one.
                    refuse("the DDS file carries a DX10 extension, which is not read.");
                    break;
                default: refuse("the DDS file carries a compression this reader does not know."); break;
            }
        }
        else if ((pixelFormatFlags & (kPixelFormatRgb | kPixelFormatAlphaPixels)) != 0u)
        {
            if (bitCount != 32u && bitCount != 24u && bitCount != 16u)
            {
                refuse("the DDS file names a bit depth this reader does not convert.");
            }
            result.format = DdsSurfaceFormat::Color;
        }
        else
        {
            refuse("the DDS pixel format names neither a compression nor channels.");
        }
        const std::uint32_t faces = result.isCube ? 6u : (result.isVolume ? result.depth : 1u);
        std::size_t at = 128u;
        result.surfaces.resize(faces);
        for (std::uint32_t face = 0u; face < faces; ++face)
        {
            result.surfaces[face].resize(result.mipCount);
        }
        // A cube stores every level of a face before the next face; a volume stores its slices
        // inside each level, which is why the slice loop is the inner one there.
        for (std::uint32_t face = 0u; face < (result.isVolume ? 1u : faces); ++face)
        {
            for (std::uint32_t level = 0u; level < result.mipCount; ++level)
            {
                const std::uint32_t levelWidth = std::max<std::uint32_t>(1u, result.width >> level);
                const std::uint32_t levelHeight = std::max<std::uint32_t>(1u, result.height >> level);
                const std::uint64_t size = LevelBytes(levelWidth, levelHeight, result.format, bitCount);
                for (std::uint32_t slice = 0u; slice < (result.isVolume ? faces : 1u); ++slice)
                {
                    if (at + size > bytes.size())
                    {
                        refuse("the DDS payload is shorter than its header describes.");
                    }
                    std::vector<std::uint8_t> surface(
                        bytes.begin() + static_cast<std::ptrdiff_t>(at),
                        bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
                    at += static_cast<std::size_t>(size);
                    if (result.format == DdsSurfaceFormat::Color)
                    {
                        // The channels are wherever the masks put them; the answer is always RGBA.
                        const std::uint32_t bytesPerPixel = bitCount / 8u;
                        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(levelWidth) * levelHeight * 4u);
                        for (std::size_t pixel = 0; pixel * bytesPerPixel + bytesPerPixel <= surface.size();
                             ++pixel)
                        {
                            std::uint32_t packed = 0u;
                            for (std::uint32_t byte = 0u; byte < bytesPerPixel; ++byte)
                            {
                                packed |= static_cast<std::uint32_t>(surface[pixel * bytesPerPixel + byte])
                                          << (8u * byte);
                            }
                            rgba[pixel * 4u] = Extract(packed, red, 0u);
                            rgba[pixel * 4u + 1u] = Extract(packed, green, 0u);
                            rgba[pixel * 4u + 2u] = Extract(packed, blue, 0u);
                            rgba[pixel * 4u + 3u] = Extract(packed, alpha, 255u);
                        }
                        surface = std::move(rgba);
                    }
                    result.surfaces[result.isVolume ? slice : face][level] = std::move(surface);
                }
            }
        }
        return result;
    }
}
