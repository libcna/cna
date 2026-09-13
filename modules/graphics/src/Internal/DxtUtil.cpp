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
         * @brief The threshold a destination texel dithers against.
         *
         * The matrix repeats over the destination every four columns and every four rows, but the
         * column it starts from on an *odd* row is not zero unless the surface is a whole number
         * of blocks wide: it is `(-width) mod 4`. Measured over level widths 1, 2, 3, 5, 6, 7, 9,
         * 11, 14, 15, 18, 22, 28, 30, 36, 44 and 88, every row of each, all three colour channels
         * and all thirty-two five-bit endpoint values, through the genuine pipeline
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-222`). The same shift governs XNA's mip
         * filter, which dithers through this matrix too.
         *
         * @param px Destination column.
         * @param py Destination row.
         * @param width The destination surface's width.
         * @return The threshold, 0 to 15.
         */
        [[nodiscard]] inline int DitherThreshold(int px, int py, int width)
        {
            const int shift = (py & 1) * ((4 - (width & 3)) & 3);
            return kDitherThreshold[py & 3][(px + shift) & 3];
        }

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
         * @param width The destination surface's width.
         * @return The byte D3DX stores.
         */
        [[nodiscard]] inline uint8_t D3dxChannel(long long numerator, long long denominator,
                                                 int px, int py, int width)
        {
            const long long base = numerator / denominator;
            const long long remainder = numerator - base * denominator;
            const int threshold = DitherThreshold(px, py, width);
            const long long value =
                base + (32 * remainder >= denominator * (2 * threshold + 1) ? 1 : 0);
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
        /**
         * @brief One texel of a colour block, exactly, before the dither narrows it.
         *
         * Every channel of every entry a block can describe is a rational over 31, 63, 93, 189, 62
         * or 126, so `kExactDenominator` -- their least common multiple -- carries all of them
         * with no rounding, which is what lets the re-encode below compare two colours exactly.
         */
        constexpr long long kExactDenominator = 11718;   // lcm(31, 63, 93, 189, 62, 126)

        /** @brief A colour block's sixteen texels, exactly, and which of them have no colour. */
        struct BlockTexels
        {
            /** @brief Each texel's three channels, over `kExactDenominator`. */
            long long channel[16][3];
            /**
             * @brief Index three of a three-colour block: black, and off the block's own line.
             *
             * It takes no part in the re-encode below -- a black that is not a colour the block
             * describes would drag the endpoints off the line every other texel sits on -- and in
             * DXT1, where the block has no alpha of its own, it is also fully transparent.
             */
            bool black[16];
        };

        /** @brief One 565 channel as a numerator over `kExactDenominator`. */
        [[nodiscard]] inline long long Exact(int value, int bits)
        {
            return static_cast<long long>(value) * 255 * (kExactDenominator / bits);
        }

        /**
         * @brief The sixteen exact colours a colour block decodes to.
         *
         * A block whose `c0` is not above its `c1` takes the three-colour rule -- index two the
         * midpoint, index three black -- and it does so in **every** DXT kind, not only in DXT1:
         * measured on DXT1, DXT3 and DXT5 alike, where only DXT1's index three is also
         * transparent, because only DXT1 has no alpha of its own
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-222`).
         *
         * @param c0 The block's first endpoint.
         * @param c1 The block's second endpoint.
         * @param lookup The block's two-bit index per texel.
         * @return The block's texels.
         */
        [[nodiscard]] BlockTexels ExactBlockTexels(uint16_t c0, uint16_t c1, uint32_t lookup)
        {
            constexpr int kBits[3] = {31, 63, 31};
            const int first[3] = {static_cast<int>((c0 >> 11) & 0x1Fu),
                                  static_cast<int>((c0 >> 5) & 0x3Fu),
                                  static_cast<int>(c0 & 0x1Fu)};
            const int second[3] = {static_cast<int>((c1 >> 11) & 0x1Fu),
                                   static_cast<int>((c1 >> 5) & 0x3Fu),
                                   static_cast<int>(c1 & 0x1Fu)};
            const bool threeColour = c0 <= c1;
            long long entry[4][3];
            bool entryBlack[4] = {false, false, false, false};
            for (int channel = 0; channel < 3; ++channel)
            {
                const long long a = Exact(first[channel], kBits[channel]);
                const long long b = Exact(second[channel], kBits[channel]);
                entry[0][channel] = a;
                entry[1][channel] = b;
                if (threeColour)
                {
                    entry[2][channel] = (a + b) / 2;
                    entry[3][channel] = 0;
                }
                else
                {
                    entry[2][channel] = (2 * a + b) / 3;
                    entry[3][channel] = (a + 2 * b) / 3;
                }
            }
            entryBlack[3] = threeColour;

            BlockTexels texels{};
            for (int texel = 0; texel < 16; ++texel)
            {
                const int index = static_cast<int>((lookup >> (2 * texel)) & 0x03u);
                for (int channel = 0; channel < 3; ++channel)
                {
                    texels.channel[texel][channel] = entry[index][channel];
                }
                texels.black[texel] = entryBlack[index];
            }
            return texels;
        }

        /**
         * @brief The 565 word nearest an exact colour, the way D3DX rounds one.
         *
         * Half to even. Only a three-colour block's midpoint entry can land on an exact half --
         * an endpoint is already a 565 value and a third of two of them never is -- and that is
         * where the even neighbour is the one the genuine pipeline answers, on 149 blocks where
         * rounding away from zero answers the other one
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-222`).
         */
        [[nodiscard]] inline uint16_t Quantize565(const long long* colour)
        {
            constexpr int kBits[3] = {31, 63, 31};
            int out[3];
            for (int channel = 0; channel < 3; ++channel)
            {
                const long long scale = static_cast<long long>(kBits[channel]);
                const long long denominator = 255 * kExactDenominator;
                const long long numerator = colour[channel] * scale;
                long long value = numerator / denominator;
                const long long remainder = numerator - value * denominator;
                if (2 * remainder > denominator ||
                    (2 * remainder == denominator && (value & 1) != 0))
                {
                    ++value;
                }
                if (value < 0) { value = 0; }
                if (value > scale) { value = scale; }
                out[channel] = static_cast<int>(value);
            }
            return static_cast<uint16_t>((out[0] << 11) | (out[1] << 5) | out[2]);
        }

        /**
         * @brief What a level whose blocks are not whole answers instead.
         *
         * D3DX cannot hold a level that is not a whole number of blocks across and down, so such a
         * level is re-encoded: the two extreme texels of each block become its endpoints,
         * quantized back to 565, and every texel then takes the nearest entry of the palette they
         * describe. A block of one colour therefore comes back as a 565 round trip of it -- which
         * is the whole of the difference at `Stripe2.dds`'s 1x1 level -- and a block whose texels
         * span its own endpoints comes back unchanged. Measured over 2,009 levels of the genuine
         * pipeline's own output; what it does not reproduce is in the row
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-222`).
         *
         * @param texels The block's texels, rewritten in place.
         */
        void RefitPartialBlock(BlockTexels& texels)
        {
            int low = -1;
            int high = -1;
            for (int texel = 0; texel < 16; ++texel)
            {
                if (texels.black[texel]) { continue; }
                const auto lexicographic = [&texels](int a, int b)
                {
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        if (texels.channel[a][channel] != texels.channel[b][channel])
                        {
                            return texels.channel[a][channel] < texels.channel[b][channel];
                        }
                    }
                    return false;
                };
                if (low < 0 || lexicographic(texel, low)) { low = texel; }
                if (high < 0 || lexicographic(high, texel)) { high = texel; }
            }
            if (low < 0) { return; }

            const uint16_t q0 = Quantize565(texels.channel[high]);
            const uint16_t q1 = Quantize565(texels.channel[low]);
            const BlockTexels rebuilt = ExactBlockTexels(q0, q1, 0xE4E4E4E4u);
            // 0xE4E4... is the lookup 0, 1, 2, 3 repeated, so `rebuilt` carries the four entries
            // of the new palette in its first four texels.
            for (int texel = 0; texel < 16; ++texel)
            {
                if (texels.black[texel]) { continue; }
                int best = 0;
                long long bestDistance = -1;
                for (int candidate = 0; candidate < 4; ++candidate)
                {
                    long long distance = 0;
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        const long long delta =
                            rebuilt.channel[candidate][channel] - texels.channel[texel][channel];
                        distance += delta * delta;
                    }
                    if (bestDistance < 0 || distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = candidate;
                    }
                }
                for (int channel = 0; channel < 3; ++channel)
                {
                    texels.channel[texel][channel] = rebuilt.channel[best][channel];
                }
            }
        }

        /** @brief True when a surface is not a whole number of blocks across and down. */
        [[nodiscard]] inline bool IsPartialSurface(int width, int height)
        {
            return (width % 4) != 0 || (height % 4) != 0;
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

        BlockTexels exact{};
        if (expansion == DxtEndpointExpansion::D3dx)
        {
            exact = ExactBlockTexels(c0, c1, lookupTable);
            if (IsPartialSurface(width, height)) { RefitPartialBlock(exact); }
        }

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
                    const int texel = 4 * blockY + blockX;
                    if (exact.black[texel])
                    {
                        a = 0;
                    }
                    else
                    {
                        r = D3dxChannel(exact.channel[texel][0], kExactDenominator, px, py, width);
                        g = D3dxChannel(exact.channel[texel][1], kExactDenominator, px, py, width);
                        b = D3dxChannel(exact.channel[texel][2], kExactDenominator, px, py, width);
                    }
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

        BlockTexels exact{};
        if (expansion == DxtEndpointExpansion::D3dx)
        {
            exact = ExactBlockTexels(c0, c1, lookupTable);
            if (IsPartialSurface(width, height)) { RefitPartialBlock(exact); }
        }

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
                    const int texel = 4 * blockY + blockX;
                    r = D3dxChannel(exact.channel[texel][0], kExactDenominator, px, py, width);
                    g = D3dxChannel(exact.channel[texel][1], kExactDenominator, px, py, width);
                    b = D3dxChannel(exact.channel[texel][2], kExactDenominator, px, py, width);
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

        BlockTexels exact{};
        if (expansion == DxtEndpointExpansion::D3dx)
        {
            exact = ExactBlockTexels(c0, c1, lookupTable);
            if (IsPartialSurface(width, height)) { RefitPartialBlock(exact); }
        }

        for (int blockY = 0; blockY < 4; ++blockY)
        {
            for (int blockX = 0; blockX < 4; ++blockX)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                uint32_t index = (lookupTable >> (2 * (4 * blockY + blockX))) & 0x03u;

                int px = (x << 2) + blockX;
                int py = (y << 2) + blockY;

                uint32_t alphaIndex =
                    static_cast<uint32_t>((alphaMask >> (3 * (4 * blockY + blockX))) & 0x07u);
                // D3DX narrows an interpolated alpha through the same ordered dither it narrows an
                // interpolated colour through -- the rule `XNASWEEP-136` measured for the colour
                // channels and this one was left out of. Over a 1,024-block probe carrying every
                // alpha index against 1,024 endpoint pairs, built by the genuine pipeline, it is
                // exact on **16,384 of 16,384** texels where truncating is exact on 60% and
                // rounding on 75% (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-220`). The
                // deviation from rounding is what named it: symmetric, +/-1, and a function of the
                // texel's position rather than of the endpoints.
                const bool dithered = expansion == DxtEndpointExpansion::D3dx;
                if (alphaIndex == 0)
                    a = alpha0;
                else if (alphaIndex == 1)
                    a = alpha1;
                else if (alpha0 > alpha1)
                {
                    const int numerator = static_cast<int>(8 - alphaIndex) * alpha0 +
                                          static_cast<int>(alphaIndex - 1) * alpha1;
                    a = dithered ? D3dxChannel(numerator, 7, px, py, width)
                                 : static_cast<uint8_t>(numerator / 7);
                }
                else if (alphaIndex == 6)
                    a = 0;
                else if (alphaIndex == 7)
                    a = 0xFF;
                else
                {
                    const int numerator = static_cast<int>(6 - alphaIndex) * alpha0 +
                                          static_cast<int>(alphaIndex - 1) * alpha1;
                    a = dithered ? D3dxChannel(numerator, 5, px, py, width)
                                 : static_cast<uint8_t>(numerator / 5);
                }
                if (expansion == DxtEndpointExpansion::D3dx)
                {
                    const int texel = 4 * blockY + blockX;
                    r = D3dxChannel(exact.channel[texel][0], kExactDenominator, px, py, width);
                    g = D3dxChannel(exact.channel[texel][1], kExactDenominator, px, py, width);
                    b = D3dxChannel(exact.channel[texel][2], kExactDenominator, px, py, width);
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
