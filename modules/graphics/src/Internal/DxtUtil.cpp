// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/DxtUtil.hpp"

#include <stdexcept>
#include <string>

namespace CNA::Internal::Graphics
{
    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    static inline uint16_t Read16(const uint8_t* data, std::size_t& pos)
    {
        uint16_t v = static_cast<uint16_t>(data[pos]) |
                     (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        return v;
    }

    static inline uint32_t Read32(const uint8_t* data, std::size_t& pos)
    {
        uint32_t v = static_cast<uint32_t>(data[pos])        |
                     (static_cast<uint32_t>(data[pos + 1]) << 8)  |
                     (static_cast<uint32_t>(data[pos + 2]) << 16) |
                     (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }

    static inline uint8_t Read8(const uint8_t* data, std::size_t& pos)
    {
        return data[pos++];
    }

    void DxtUtil::ConvertRgb565ToRgb888(uint16_t color,
                                         uint8_t& r, uint8_t& g, uint8_t& b)
    {
        int temp;
        temp = (color >> 11) * 255 + 16;
        r = static_cast<uint8_t>((temp / 32 + temp) / 32);
        temp = ((color & 0x07E0) >> 5) * 255 + 32;
        g = static_cast<uint8_t>((temp / 64 + temp) / 64);
        temp = (color & 0x001F) * 255 + 16;
        b = static_cast<uint8_t>((temp / 32 + temp) / 32);
    }

    // ---------------------------------------------------------------------------
    // DXT1
    // ---------------------------------------------------------------------------

    void DxtUtil::DecompressDxt1Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData)
    {
        uint16_t c0 = Read16(data, pos);
        uint16_t c1 = Read16(data, pos);

        uint8_t r0, g0, b0, r1, g1, b1;
        ConvertRgb565ToRgb888(c0, r0, g0, b0);
        ConvertRgb565ToRgb888(c1, r1, g1, b1);

        uint32_t lookupTable = Read32(data, pos);

        for (int blockY = 0; blockY < 4; ++blockY)
        {
            for (int blockX = 0; blockX < 4; ++blockX)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                uint32_t index = (lookupTable >> (2 * (4 * blockY + blockX))) & 0x03u;

                if (c0 > c1)
                {
                    switch (index)
                    {
                    case 0: r = r0; g = g0; b = b0; break;
                    case 1: r = r1; g = g1; b = b1; break;
                    case 2:
                        r = static_cast<uint8_t>((2 * r0 + r1) / 3);
                        g = static_cast<uint8_t>((2 * g0 + g1) / 3);
                        b = static_cast<uint8_t>((2 * b0 + b1) / 3);
                        break;
                    case 3:
                        r = static_cast<uint8_t>((r0 + 2 * r1) / 3);
                        g = static_cast<uint8_t>((g0 + 2 * g1) / 3);
                        b = static_cast<uint8_t>((b0 + 2 * b1) / 3);
                        break;
                    }
                }
                else
                {
                    switch (index)
                    {
                    case 0: r = r0; g = g0; b = b0; break;
                    case 1: r = r1; g = g1; b = b1; break;
                    case 2:
                        r = static_cast<uint8_t>((r0 + r1) / 2);
                        g = static_cast<uint8_t>((g0 + g1) / 2);
                        b = static_cast<uint8_t>((b0 + b1) / 2);
                        break;
                    case 3: r = 0; g = 0; b = 0; a = 0; break;
                    }
                }

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;
                if (px < width && py < height)
                {
                    int offset = ((py * width) + px) << 2;
                    imageData[offset]     = r;
                    imageData[offset + 1] = g;
                    imageData[offset + 2] = b;
                    imageData[offset + 3] = a;
                }
            }
        }
    }

    std::vector<uint8_t> DxtUtil::DecompressDxt1(const uint8_t* data, std::size_t dataSize,
                                                   int width, int height)
    {
        const int blockCountX = (width  + 3) / 4;
        const int blockCountY = (height + 3) / 4;
        // DXT1 is 8 bytes/block (2x uint16 endpoint color + 1x uint32 index lookup). Reject a
        // too-small dataSize up front, before any block read -- Read8/16/32 do not themselves
        // bounds-check pos against dataSize, so an under-sized buffer (e.g. from a truncated or
        // adversarial .xnb whose declared byteCount doesn't match its own width/height) would
        // otherwise read past the end of data.
        const std::size_t requiredBytes = static_cast<std::size_t>(blockCountX) * blockCountY * 8;
        if (dataSize < requiredBytes)
        {
            throw std::out_of_range(
                "DxtUtil::DecompressDxt1: dataSize (" + std::to_string(dataSize) +
                ") is smaller than the " + std::to_string(requiredBytes) +
                " bytes " + std::to_string(width) + "x" + std::to_string(height) + " requires.");
        }

        std::vector<uint8_t> out(static_cast<std::size_t>(width * height * 4), 0);
        std::size_t pos = 0;
        for (int y = 0; y < blockCountY; ++y)
            for (int x = 0; x < blockCountX; ++x)
                DecompressDxt1Block(data, pos, x, y, width, height, out.data());
        return out;
    }

    // ---------------------------------------------------------------------------
    // DXT3
    // ---------------------------------------------------------------------------

    void DxtUtil::DecompressDxt3Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData)
    {
        uint8_t a[8];
        for (int i = 0; i < 8; ++i) a[i] = Read8(data, pos);

        uint16_t c0 = Read16(data, pos);
        uint16_t c1 = Read16(data, pos);

        uint8_t r0, g0, b0, r1, g1, b1;
        ConvertRgb565ToRgb888(c0, r0, g0, b0);
        ConvertRgb565ToRgb888(c1, r1, g1, b1);

        uint32_t lookupTable = Read32(data, pos);

        int alphaIndex = 0;
        for (int blockY = 0; blockY < 4; ++blockY)
        {
            for (int blockX = 0; blockX < 4; ++blockX)
            {
                uint8_t r = 0, g = 0, b = 0, alpha = 0;
                uint32_t index = (lookupTable >> (2 * (4 * blockY + blockX))) & 0x03u;

                const uint8_t ab = a[alphaIndex / 2];
                if ((alphaIndex & 1) == 0)
                    alpha = static_cast<uint8_t>((ab & 0x0F) | ((ab & 0x0F) << 4));
                else
                    alpha = static_cast<uint8_t>((ab & 0xF0) | ((ab & 0xF0) >> 4));
                ++alphaIndex;

                switch (index)
                {
                case 0: r = r0; g = g0; b = b0; break;
                case 1: r = r1; g = g1; b = b1; break;
                case 2:
                    r = static_cast<uint8_t>((2 * r0 + r1) / 3);
                    g = static_cast<uint8_t>((2 * g0 + g1) / 3);
                    b = static_cast<uint8_t>((2 * b0 + b1) / 3);
                    break;
                case 3:
                    r = static_cast<uint8_t>((r0 + 2 * r1) / 3);
                    g = static_cast<uint8_t>((g0 + 2 * g1) / 3);
                    b = static_cast<uint8_t>((b0 + 2 * b1) / 3);
                    break;
                }

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;
                if (px < width && py < height)
                {
                    int offset = ((py * width) + px) << 2;
                    imageData[offset]     = r;
                    imageData[offset + 1] = g;
                    imageData[offset + 2] = b;
                    imageData[offset + 3] = alpha;
                }
            }
        }
    }

    std::vector<uint8_t> DxtUtil::DecompressDxt3(const uint8_t* data, std::size_t dataSize,
                                                   int width, int height)
    {
        const int blockCountX = (width  + 3) / 4;
        const int blockCountY = (height + 3) / 4;
        // DXT3 is 16 bytes/block (8 explicit alpha + 2x uint16 endpoint color + 1x uint32 index
        // lookup) -- see DecompressDxt1's own note on why this upfront check exists.
        const std::size_t requiredBytes = static_cast<std::size_t>(blockCountX) * blockCountY * 16;
        if (dataSize < requiredBytes)
        {
            throw std::out_of_range(
                "DxtUtil::DecompressDxt3: dataSize (" + std::to_string(dataSize) +
                ") is smaller than the " + std::to_string(requiredBytes) +
                " bytes " + std::to_string(width) + "x" + std::to_string(height) + " requires.");
        }

        std::vector<uint8_t> out(static_cast<std::size_t>(width * height * 4), 0);
        std::size_t pos = 0;
        for (int y = 0; y < blockCountY; ++y)
            for (int x = 0; x < blockCountX; ++x)
                DecompressDxt3Block(data, pos, x, y, width, height, out.data());
        return out;
    }

    // ---------------------------------------------------------------------------
    // DXT5
    // ---------------------------------------------------------------------------

    void DxtUtil::DecompressDxt5Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData)
    {
        uint8_t alpha0 = Read8(data, pos);
        uint8_t alpha1 = Read8(data, pos);

        uint64_t alphaMask = static_cast<uint64_t>(Read8(data, pos));
        alphaMask |= static_cast<uint64_t>(Read8(data, pos)) << 8;
        alphaMask |= static_cast<uint64_t>(Read8(data, pos)) << 16;
        alphaMask |= static_cast<uint64_t>(Read8(data, pos)) << 24;
        alphaMask |= static_cast<uint64_t>(Read8(data, pos)) << 32;
        alphaMask |= static_cast<uint64_t>(Read8(data, pos)) << 40;

        uint16_t c0 = Read16(data, pos);
        uint16_t c1 = Read16(data, pos);

        uint8_t r0, g0, b0, r1, g1, b1;
        ConvertRgb565ToRgb888(c0, r0, g0, b0);
        ConvertRgb565ToRgb888(c1, r1, g1, b1);

        uint32_t lookupTable = Read32(data, pos);

        for (int blockY = 0; blockY < 4; ++blockY)
        {
            for (int blockX = 0; blockX < 4; ++blockX)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                uint32_t index = (lookupTable >> (2 * (4 * blockY + blockX))) & 0x03u;

                uint32_t alphaIndex =
                    static_cast<uint32_t>((alphaMask >> (3 * (4 * blockY + blockX))) & 0x07u);
                if (alphaIndex == 0)
                    a = alpha0;
                else if (alphaIndex == 1)
                    a = alpha1;
                else if (alpha0 > alpha1)
                    a = static_cast<uint8_t>(((8 - alphaIndex) * alpha0 + (alphaIndex - 1) * alpha1) / 7);
                else if (alphaIndex == 6)
                    a = 0;
                else if (alphaIndex == 7)
                    a = 0xFF;
                else
                    a = static_cast<uint8_t>(((6 - alphaIndex) * alpha0 + (alphaIndex - 1) * alpha1) / 5);

                switch (index)
                {
                case 0: r = r0; g = g0; b = b0; break;
                case 1: r = r1; g = g1; b = b1; break;
                case 2:
                    r = static_cast<uint8_t>((2 * r0 + r1) / 3);
                    g = static_cast<uint8_t>((2 * g0 + g1) / 3);
                    b = static_cast<uint8_t>((2 * b0 + b1) / 3);
                    break;
                case 3:
                    r = static_cast<uint8_t>((r0 + 2 * r1) / 3);
                    g = static_cast<uint8_t>((g0 + 2 * g1) / 3);
                    b = static_cast<uint8_t>((b0 + 2 * b1) / 3);
                    break;
                }

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;
                if (px < width && py < height)
                {
                    int offset = ((py * width) + px) << 2;
                    imageData[offset]     = r;
                    imageData[offset + 1] = g;
                    imageData[offset + 2] = b;
                    imageData[offset + 3] = a;
                }
            }
        }
    }

    std::vector<uint8_t> DxtUtil::DecompressDxt5(const uint8_t* data, std::size_t dataSize,
                                                   int width, int height)
    {
        const int blockCountX = (width  + 3) / 4;
        const int blockCountY = (height + 3) / 4;
        // DXT5 is 16 bytes/block (2x uint8 alpha endpoint + 6x uint8 alpha index mask + 2x uint16
        // endpoint color + 1x uint32 index lookup) -- see DecompressDxt1's own note on why this
        // upfront check exists.
        const std::size_t requiredBytes = static_cast<std::size_t>(blockCountX) * blockCountY * 16;
        if (dataSize < requiredBytes)
        {
            throw std::out_of_range(
                "DxtUtil::DecompressDxt5: dataSize (" + std::to_string(dataSize) +
                ") is smaller than the " + std::to_string(requiredBytes) +
                " bytes " + std::to_string(width) + "x" + std::to_string(height) + " requires.");
        }

        std::vector<uint8_t> out(static_cast<std::size_t>(width * height * 4), 0);
        std::size_t pos = 0;
        for (int y = 0; y < blockCountY; ++y)
            for (int x = 0; x < blockCountX; ++x)
                DecompressDxt5Block(data, pos, x, y, width, height, out.data());
        return out;
    }
}
