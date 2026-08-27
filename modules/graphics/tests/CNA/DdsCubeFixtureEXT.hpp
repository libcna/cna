// SPDX-License-Identifier: MS-PL
#pragma once

// Shared DDS cube-map fixture builder (plans/plan_cnb.md `CNBF-113`).
//
// Extracted from TextureCubeTests.cpp so the CNB producer tests build the SAME bytes the runtime
// DDS tests do. Two hand-built DDS writers would be two definitions of "a valid cube map", and the
// first time they disagreed the disagreement would look like a decoder bug.
//
// Test scaffolding, deliberately not in any module's public include tree.

#include <cstdint>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"

namespace CNA::TestSupport
{
    namespace DdsFixtureDetail
    {
        inline void PushU32LE(std::vector<std::uint8_t>& v, std::uint32_t x)
        {
            v.push_back(static_cast<std::uint8_t>(x & 0xFF));
            v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFF));
            v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFF));
            v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xFF));
        }

        inline void PushAscii4(std::vector<std::uint8_t>& v, const char* s)
        {
            for (int i = 0; i < 4; ++i) { v.push_back(static_cast<std::uint8_t>(s[i])); }
        }
    }

    /** @brief DDS `caps` bit marking the file a texture. */
    inline constexpr std::uint32_t kDdsCapsTexture = 0x1000u;
    /** @brief DDS `caps` bit marking a mip chain present. */
    inline constexpr std::uint32_t kDdsCapsMipmap = 0x400000u;
    /** @brief DDS `caps2` bit marking the file a cube map. */
    inline constexpr std::uint32_t kDdsCaps2Cubemap = 0x200u;
    /** @brief All six DDSCAPS2_CUBEMAP_* face bits. */
    inline constexpr std::uint32_t kDdsCaps2AllFaces = 0xFC00u;

    /**
     * @brief Packs an exactly-RGB565-representable colour into one solid 8-byte DXT1 block.
     *
     * `color0 == color1 ==` the target colour and every 2-bit index is 0, which the decompressor
     * maps to `color0` in both of DXT1's comparison modes. Each channel must already be 0 or 255,
     * so the round trip through RGB565 is exact and the test can assert on precise values.
     *
     * @param v Byte vector to append to.
     * @param c The colour.
     */
    inline void PushSolidDxt1Block(std::vector<std::uint8_t>& v,
                                    const Microsoft::Xna::Framework::Color& c)
    {
        const auto rgb565 = static_cast<std::uint16_t>(
            ((static_cast<std::uint32_t>(c.getRProperty()) >> 3) << 11) |
            ((static_cast<std::uint32_t>(c.getGProperty()) >> 2) << 5) |
            (static_cast<std::uint32_t>(c.getBProperty()) >> 3));
        v.push_back(static_cast<std::uint8_t>(rgb565 & 0xFF));
        v.push_back(static_cast<std::uint8_t>((rgb565 >> 8) & 0xFF));
        v.push_back(static_cast<std::uint8_t>(rgb565 & 0xFF));
        v.push_back(static_cast<std::uint8_t>((rgb565 >> 8) & 0xFF));
        v.push_back(0); v.push_back(0); v.push_back(0); v.push_back(0);
    }

    /**
     * @brief Appends one solid DXT3 block: opaque alpha, then the same colour payload as DXT1.
     *
     * @param v Byte vector to append to.
     * @param c The colour.
     */
    inline void PushSolidDxt3Block(std::vector<std::uint8_t>& v,
                                    const Microsoft::Xna::Framework::Color& c)
    {
        for (int i = 0; i < 8; ++i) { v.push_back(0xFF); } // 4-bit alpha, all 15 -> opaque
        PushSolidDxt1Block(v, c);
    }

    /**
     * @brief Appends one solid DXT5 block: a constant alpha ramp, then the DXT1 colour payload.
     *
     * `alpha0 == alpha1 == 255` with all-zero indices selects `alpha0` everywhere, so the block is
     * fully opaque regardless of which interpolation branch the decompressor takes.
     *
     * @param v Byte vector to append to.
     * @param c The colour.
     */
    inline void PushSolidDxt5Block(std::vector<std::uint8_t>& v,
                                    const Microsoft::Xna::Framework::Color& c)
    {
        v.push_back(0xFF); v.push_back(0xFF);                       // alpha0, alpha1
        for (int i = 0; i < 6; ++i) { v.push_back(0); }              // 3-bit indices, all 0
        PushSolidDxt1Block(v, c);
    }

    /** @brief Which block codec BuildSolidColorCubeDds() should write. */
    enum class DdsBlockFormat
    {
        Dxt1,
        Dxt3,
        Dxt5,
        /** @brief A FourCC outside the supported set, for negative tests. */
        UnsupportedFourCc,
        /** @brief No FourCC flag at all, for negative tests. */
        NoFourCc,
    };

    /**
     * @brief Builds a minimal, valid DDS cube map with one solid colour per face and mip level.
     *
     * `faceColors[0..5]` map to `CubeMapFace::PositiveX..NegativeZ` in order, matching the on-disk
     * DDS face layout. Every mip level of a face repeats that face's colour, so a test can assert
     * on any level without a second expectation table.
     *
     * @param size       Face width and height at level 0; must be a multiple of 4 for the block
     *                   maths to stay exact.
     * @param faceColors Six colours, each channel already 0 or 255.
     * @param mipCount   Number of mip levels to write; 1 writes no mip chain at all.
     * @param format     Block codec, or one of the deliberately-invalid variants.
     * @param asCubeMap  False writes a non-cube DDS, for negative tests.
     * @return The complete DDS file bytes.
     */
    inline std::vector<std::uint8_t> BuildSolidColorCubeDds(
        int size, const Microsoft::Xna::Framework::Color faceColors[6], int mipCount = 1,
        DdsBlockFormat format = DdsBlockFormat::Dxt1, bool asCubeMap = true)
    {
        using namespace DdsFixtureDetail;
        std::vector<std::uint8_t> d;
        PushAscii4(d, "DDS ");
        PushU32LE(d, 124);
        PushU32LE(d, 0x2u | 0x4u);                          // DDSD_HEIGHT | DDSD_WIDTH
        PushU32LE(d, static_cast<std::uint32_t>(size));      // height
        PushU32LE(d, static_cast<std::uint32_t>(size));      // width
        PushU32LE(d, 0);                                     // pitchOrLinearSize
        PushU32LE(d, 0);                                     // depth
        PushU32LE(d, static_cast<std::uint32_t>(mipCount));  // mipMapCount
        for (int i = 0; i < 11; ++i) { PushU32LE(d, 0); }    // reserved1

        PushU32LE(d, 32);                                    // pixel format size
        const bool haveFourCc = format != DdsBlockFormat::NoFourCc;
        PushU32LE(d, haveFourCc ? 0x4u : 0u);                // DDPF_FOURCC
        switch (format)
        {
            case DdsBlockFormat::Dxt1: PushAscii4(d, "DXT1"); break;
            case DdsBlockFormat::Dxt3: PushAscii4(d, "DXT3"); break;
            case DdsBlockFormat::Dxt5: PushAscii4(d, "DXT5"); break;
            case DdsBlockFormat::UnsupportedFourCc: PushAscii4(d, "DX10"); break;
            case DdsBlockFormat::NoFourCc: PushU32LE(d, 0); break;
        }
        for (int i = 0; i < 5; ++i) { PushU32LE(d, 0); }     // bit count and masks

        PushU32LE(d, kDdsCapsTexture | (mipCount > 1 ? kDdsCapsMipmap : 0u));
        PushU32LE(d, asCubeMap ? (kDdsCaps2Cubemap | kDdsCaps2AllFaces) : 0u);
        PushU32LE(d, 0);                                     // caps3
        PushU32LE(d, 0);                                     // caps4
        PushU32LE(d, 0);                                     // reserved2

        for (int face = 0; face < 6; ++face)
        {
            int levelSize = size;
            for (int level = 0; level < mipCount; ++level)
            {
                const int blocks = ((levelSize + 3) / 4) * ((levelSize + 3) / 4);
                for (int b = 0; b < blocks; ++b)
                {
                    switch (format)
                    {
                        case DdsBlockFormat::Dxt3: PushSolidDxt3Block(d, faceColors[face]); break;
                        case DdsBlockFormat::Dxt5: PushSolidDxt5Block(d, faceColors[face]); break;
                        default: PushSolidDxt1Block(d, faceColors[face]); break;
                    }
                }
                levelSize = levelSize > 1 ? levelSize / 2 : 1;
            }
        }
        return d;
    }
}
