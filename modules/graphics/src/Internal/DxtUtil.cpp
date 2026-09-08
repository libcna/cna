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

    namespace
    {
        /**
         * @brief The ordered 4x4 threshold D3DX dithers a block's colours through.
         *
         * Measured by sweeping every 5- and 6-bit endpoint value through the genuine pipeline: a
         * flat block of endpoint 1 comes out 9 in four of its sixteen texels and 8 in the other
         * twelve, in the pattern this matrix describes, and endpoint 2 comes out 17 in seven of
         * them. Rounding to nearest answers 8 and 16 everywhere and is wrong on 128 of 2,048
         * texels; this matrix is wrong on none (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-136`).
         * It is the same matrix XNA's own mip filter dithers through.
         */
        constexpr int kDitherThreshold[4][4] = {
            {0, 8, 2, 10}, {6, 14, 4, 12}, {3, 11, 1, 9}, {5, 13, 7, 15}};

        /**
         * @brief One channel of a block's texel, as D3DX answers it.
         *
         * The endpoint channels are not expanded to bytes and then interpolated: the value is the
         * exact rational the block describes -- `255 * n / d` for an endpoint, and the exact third
         * or half of two of those for an interpolated colour -- and only the *store* narrows it,
         * through the ordered dither. Expanding first loses the fraction the dither needs.
         *
         * @param numerator The value's exact numerator.
         * @param denominator The value's exact denominator.
         * @param px Destination column, which with @p py selects the threshold.
         * @param py Destination row.
         * @return The byte D3DX stores.
         */
        [[nodiscard]] inline uint8_t D3dxChannel(int numerator, int denominator, int px, int py)
        {
            const int base = numerator / denominator;
            const int remainder = numerator - base * denominator;
            const int threshold = kDitherThreshold[py & 3][px & 3];
            const int value = base + (32 * remainder >= denominator * (2 * threshold + 1) ? 1 : 0);
            return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
        }
    }

    void DxtUtil::ConvertRgb565ToRgb888(uint16_t color,
                                         uint8_t& r, uint8_t& g, uint8_t& b,
                                         DxtEndpointExpansion expansion)
    {
        if (expansion == DxtEndpointExpansion::D3dx)
        {
            // Measured over every endpoint value: 5-bit 1 answers 9 and 2 answers 17, where
            // rounding to nearest answers 8 and 16 (XNASWEEP-136).
            r = static_cast<uint8_t>(((color >> 11) * 255 + 30) / 31);
            g = static_cast<uint8_t>((((color & 0x07E0) >> 5) * 255 + 62) / 63);
            b = static_cast<uint8_t>(((color & 0x001F) * 255 + 30) / 31);
            return;
        }
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

    namespace
    {
        /** @brief The three channels of a 565 endpoint, as (numerator, denominator) pairs. */
        struct EndpointChannels
        {
            int numerator[3];
            int denominator[3];
        };

        [[nodiscard]] inline EndpointChannels Channels(uint16_t colour)
        {
            EndpointChannels out{};
            out.numerator[0] = static_cast<int>((colour >> 11) & 0x1Fu) * 255;
            out.denominator[0] = 31;
            out.numerator[1] = static_cast<int>((colour >> 5) & 0x3Fu) * 255;
            out.denominator[1] = 63;
            out.numerator[2] = static_cast<int>(colour & 0x1Fu) * 255;
            out.denominator[2] = 31;
            return out;
        }

        /**
         * @brief One texel of a colour block through D3DX's rule.
         *
         * @param first The first endpoint's channels.
         * @param second The second endpoint's channels.
         * @param weightFirst Numerator of the first endpoint's weight.
         * @param weightSecond Numerator of the second endpoint's weight.
         * @param weightTotal The two weights' denominator: 1 for an endpoint, 3 or 2 for a blend.
         * @param px Destination column.
         * @param py Destination row.
         * @param rgb Receives the three bytes.
         */
        inline void D3dxTexel(const EndpointChannels& first, const EndpointChannels& second,
                              int weightFirst, int weightSecond, int weightTotal, int px, int py,
                              uint8_t* rgb)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                const int numerator = weightFirst * first.numerator[channel] * second.denominator[channel] +
                                      weightSecond * second.numerator[channel] * first.denominator[channel];
                const int denominator =
                    weightTotal * first.denominator[channel] * second.denominator[channel];
                rgb[channel] = D3dxChannel(numerator, denominator, px, py);
            }
        }
    }

    void DxtUtil::DecompressDxt1Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData,
                                        DxtEndpointExpansion expansion)
    {
        uint16_t c0 = Read16(data, pos);
        uint16_t c1 = Read16(data, pos);

        uint8_t r0, g0, b0, r1, g1, b1;
        ConvertRgb565ToRgb888(c0, r0, g0, b0, expansion);
        ConvertRgb565ToRgb888(c1, r1, g1, b1, expansion);

        uint32_t lookupTable = Read32(data, pos);

        for (int blockY = 0; blockY < 4; ++blockY)
        {
            for (int blockX = 0; blockX < 4; ++blockX)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                uint32_t index = (lookupTable >> (2 * (4 * blockY + blockX))) & 0x03u;
                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;

                if (expansion == DxtEndpointExpansion::D3dx)
                {
                    const EndpointChannels first = Channels(c0);
                    const EndpointChannels second = Channels(c1);
                    uint8_t rgb[3] = {0, 0, 0};
                    if (index == 3u && c0 <= c1)
                    {
                        a = 0;
                    }
                    else if (index == 0u)
                    {
                        D3dxTexel(first, second, 1, 0, 1, px, py, rgb);
                    }
                    else if (index == 1u)
                    {
                        D3dxTexel(first, second, 0, 1, 1, px, py, rgb);
                    }
                    else if (c0 > c1)
                    {
                        D3dxTexel(first, second, index == 2u ? 2 : 1, index == 2u ? 1 : 2, 3, px,
                                  py, rgb);
                    }
                    else
                    {
                        D3dxTexel(first, second, 1, 1, 2, px, py, rgb);
                    }
                    r = rgb[0];
                    g = rgb[1];
                    b = rgb[2];
                }
                else if (c0 > c1)
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
                                                   int width, int height,
                                                    DxtEndpointExpansion expansion)
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
                DecompressDxt1Block(data, pos, x, y, width, height, out.data(), expansion);
        return out;
    }

    // ---------------------------------------------------------------------------
    // DXT3
    // ---------------------------------------------------------------------------

    void DxtUtil::DecompressDxt3Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData,
                                        DxtEndpointExpansion expansion)
    {
        uint8_t a[8];
        for (int i = 0; i < 8; ++i) a[i] = Read8(data, pos);

        uint16_t c0 = Read16(data, pos);
        uint16_t c1 = Read16(data, pos);

        uint8_t r0, g0, b0, r1, g1, b1;
        ConvertRgb565ToRgb888(c0, r0, g0, b0, expansion);
        ConvertRgb565ToRgb888(c1, r1, g1, b1, expansion);

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

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;
                if (expansion == DxtEndpointExpansion::D3dx)
                {
                    const EndpointChannels first = Channels(c0);
                    const EndpointChannels second = Channels(c1);
                    uint8_t rgb[3] = {0, 0, 0};
                    const int weightFirst = index == 0u ? 1 : (index == 1u ? 0 : (index == 2u ? 2 : 1));
                    const int weightSecond = index == 0u ? 0 : (index == 1u ? 1 : (index == 2u ? 1 : 2));
                    const int weightTotal = index < 2u ? 1 : 3;
                    D3dxTexel(first, second, weightFirst, weightSecond, weightTotal, px, py, rgb);
                    r = rgb[0];
                    g = rgb[1];
                    b = rgb[2];
                }
                else
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
                                                   int width, int height,
                                                    DxtEndpointExpansion expansion)
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
                DecompressDxt3Block(data, pos, x, y, width, height, out.data(), expansion);
        return out;
    }

    // ---------------------------------------------------------------------------
    // DXT5
    // ---------------------------------------------------------------------------

    void DxtUtil::DecompressDxt5Block(const uint8_t* data, std::size_t& pos,
                                       int x, int y, int width, int height,
                                       uint8_t* imageData,
                                        DxtEndpointExpansion expansion)
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
        ConvertRgb565ToRgb888(c0, r0, g0, b0, expansion);
        ConvertRgb565ToRgb888(c1, r1, g1, b1, expansion);

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

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;
                if (expansion == DxtEndpointExpansion::D3dx)
                {
                    const EndpointChannels first = Channels(c0);
                    const EndpointChannels second = Channels(c1);
                    uint8_t rgb[3] = {0, 0, 0};
                    const int weightFirst = index == 0u ? 1 : (index == 1u ? 0 : (index == 2u ? 2 : 1));
                    const int weightSecond = index == 0u ? 0 : (index == 1u ? 1 : (index == 2u ? 1 : 2));
                    const int weightTotal = index < 2u ? 1 : 3;
                    D3dxTexel(first, second, weightFirst, weightSecond, weightTotal, px, py, rgb);
                    r = rgb[0];
                    g = rgb[1];
                    b = rgb[2];
                }
                else
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
                                                   int width, int height,
                                                    DxtEndpointExpansion expansion)
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
                DecompressDxt5Block(data, pos, x, y, width, height, out.data(), expansion);
        return out;
    }
}
