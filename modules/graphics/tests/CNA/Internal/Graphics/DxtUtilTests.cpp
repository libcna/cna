// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>
#include "CNA/Internal/Graphics/DxtUtil.hpp"

using CNA::Internal::Graphics::DxtUtil;

// DXT1 block (8 bytes): c0=0xF800 (red), c1=0x0000 (black), lookup=0 (all index-0)
// With c0 > c1: 4-color mode. All pixels select c0 → RGB=(255,0,0), A=255.
static const uint8_t kSolidRedDxt1[8] = {
    0x00, 0xF8,             // c0 = 0xF800 (R=31,G=0,B=0 in RGB565)
    0x00, 0x00,             // c1 = 0x0000 (black)
    0x00, 0x00, 0x00, 0x00  // lookup: all pixels use index 0
};

// DXT5 block (16 bytes): alpha0=255, alpha1=0, alphaMask=0 (all alpha=255)
// Color part identical to kSolidRedDxt1 → all pixels (255,0,0,255).
static const uint8_t kSolidRedDxt5[16] = {
    0xFF, 0x00,             // alpha0=255, alpha1=0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // alphaMask (48 bits) = 0 → all use alpha0=255
    0x00, 0xF8,             // c0 = 0xF800
    0x00, 0x00,             // c1 = 0x0000
    0x00, 0x00, 0x00, 0x00  // lookup: all index 0
};

// DXT3 block (16 bytes): explicit 4-bit alpha, all nibbles = 0xF (→ 0xFF after expand)
// Color part same as solid red.
static const uint8_t kSolidRedDxt3[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // alpha: all nibbles = 0xF → 255
    0x00, 0xF8,             // c0 = 0xF800
    0x00, 0x00,             // c1 = 0x0000
    0x00, 0x00, 0x00, 0x00  // lookup: all index 0
};

TEST(DxtUtil, DecompressDxt1_SolidRed_4x4)
{
    auto pixels = DxtUtil::DecompressDxt1(kSolidRedDxt1, sizeof(kSolidRedDxt1), 4, 4);
    ASSERT_EQ(pixels.size(), std::size_t(4 * 4 * 4));
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_EQ(pixels[i * 4 + 0], 255u) << "R at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 1],   0u) << "G at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 2],   0u) << "B at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 3], 255u) << "A at pixel " << i;
    }
}

TEST(DxtUtil, DecompressDxt1_TransparentPixel_WhenC0_le_C1_Index3)
{
    // c0 < c1: 3-color + transparent mode. index=3 → RGBA=(0,0,0,0).
    // All pixels select index 3 (lookup=0xFFFFFFFF).
    uint8_t block[8] = {
        0x00, 0x00,             // c0 = 0x0000 (black)
        0x00, 0xF8,             // c1 = 0xF800 (red)  → c0 <= c1
        0xFF, 0xFF, 0xFF, 0xFF  // lookup: all index 3
    };
    auto pixels = DxtUtil::DecompressDxt1(block, sizeof(block), 4, 4);
    ASSERT_EQ(pixels.size(), std::size_t(4 * 4 * 4));
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_EQ(pixels[i * 4 + 3], 0u) << "A must be 0 (transparent) at pixel " << i;
    }
}

TEST(DxtUtil, DecompressDxt3_SolidRed_4x4)
{
    auto pixels = DxtUtil::DecompressDxt3(kSolidRedDxt3, sizeof(kSolidRedDxt3), 4, 4);
    ASSERT_EQ(pixels.size(), std::size_t(4 * 4 * 4));
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_EQ(pixels[i * 4 + 0], 255u) << "R at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 1],   0u) << "G at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 2],   0u) << "B at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 3], 255u) << "A at pixel " << i;
    }
}

TEST(DxtUtil, DecompressDxt5_SolidRed_4x4)
{
    auto pixels = DxtUtil::DecompressDxt5(kSolidRedDxt5, sizeof(kSolidRedDxt5), 4, 4);
    ASSERT_EQ(pixels.size(), std::size_t(4 * 4 * 4));
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_EQ(pixels[i * 4 + 0], 255u) << "R at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 1],   0u) << "G at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 2],   0u) << "B at pixel " << i;
        EXPECT_EQ(pixels[i * 4 + 3], 255u) << "A at pixel " << i;
    }
}

TEST(DxtUtil, DecompressDxt5_PartialAlpha)
{
    // alpha0=128, alpha1=0, alphaMask all 0 → all pixels have alpha=128
    uint8_t block[16] = {
        0x80, 0x00,             // alpha0=128, alpha1=0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xF8,             // c0 = red
        0x00, 0x00,             // c1 = black
        0x00, 0x00, 0x00, 0x00
    };
    auto pixels = DxtUtil::DecompressDxt5(block, sizeof(block), 4, 4);
    ASSERT_EQ(pixels.size(), std::size_t(4 * 4 * 4));
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(pixels[i * 4 + 3], 128u) << "A at pixel " << i;
}

TEST(DxtUtil, DecompressDxt1_NonSquare_Width8_Height4)
{
    // 8×4 image = 2 DXT1 blocks horizontally, 1 row of blocks
    const uint8_t blocks[16] = {
        // block (0,0): solid red
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // block (1,0): solid green (0x07E0)
        0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    auto pixels = DxtUtil::DecompressDxt1(blocks, sizeof(blocks), 8, 4);
    ASSERT_EQ(pixels.size(), std::size_t(8 * 4 * 4));

    // Left 4 columns (x < 4) = red
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            int off = (y * 8 + x) * 4;
            EXPECT_EQ(pixels[off + 0], 255u) << "R at (" << x << "," << y << ")";
            EXPECT_EQ(pixels[off + 1],   0u) << "G at (" << x << "," << y << ")";
        }

    // Right 4 columns (x >= 4) = green
    for (int y = 0; y < 4; ++y)
        for (int x = 4; x < 8; ++x)
        {
            int off = (y * 8 + x) * 4;
            EXPECT_EQ(pixels[off + 0],   0u) << "R at (" << x << "," << y << ")";
            EXPECT_EQ(pixels[off + 1], 255u) << "G at (" << x << "," << y << ")";
        }
}

// plans/plan_xnb.md XNB-43/47: a dataSize too small for the requested width/height must be rejected
// before any block read, not discovered mid-decode -- found via a whole-container fuzz test that
// mutated a real .xnb's own declared byteCount field, confirmed as a real heap-buffer-overflow
// under -DCNA_SANITIZE=address,undefined (Read8/16/32 never themselves bounds-checked pos against
// dataSize).

TEST(DxtUtil, DecompressDxt1_DataSizeTooSmall_ThrowsOutOfRange)
{
    // 8x4 needs 2 DXT1 blocks = 16 bytes; only provide 8.
    EXPECT_THROW(DxtUtil::DecompressDxt1(kSolidRedDxt1, 8, 8, 4), std::out_of_range);
}

TEST(DxtUtil, DecompressDxt3_DataSizeTooSmall_ThrowsOutOfRange)
{
    // 8x4 needs 2 DXT3 blocks = 32 bytes; only provide 16.
    EXPECT_THROW(DxtUtil::DecompressDxt3(kSolidRedDxt3, 16, 8, 4), std::out_of_range);
}

TEST(DxtUtil, DecompressDxt5_DataSizeTooSmall_ThrowsOutOfRange)
{
    // 8x4 needs 2 DXT5 blocks = 32 bytes; only provide 16.
    EXPECT_THROW(DxtUtil::DecompressDxt5(kSolidRedDxt5, 16, 8, 4), std::out_of_range);
}

TEST(DxtUtil, DecompressDxt1_ExactlyEnoughData_DoesNotThrow)
{
    EXPECT_NO_THROW(DxtUtil::DecompressDxt1(kSolidRedDxt1, sizeof(kSolidRedDxt1), 4, 4));
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-220: D3DX narrows an interpolated *alpha* through
// the same ordered dither it narrows an interpolated colour through.
//
// `XNASWEEP-136` measured that dither for the colour channels and this channel was left out of it,
// so an interpolated alpha was the truncated quotient. A 1,024-block probe built by the genuine
// pipeline says otherwise: truncating is exact on 60% of its interpolated texels and rounding on
// 75%, and the deviation from rounding is what names the rule -- symmetric, plus or minus one, and
// a function of the texel's *position* rather than of the endpoints. Through `D3dxChannel` it is
// exact on all 16,384.
//
// One block of that probe, at the position it occupies in it, so the dither's threshold is the one
// it was measured with.
TEST(DxtUtilTests, AnInterpolatedAlphaTakesTheSameOrderedDitherAnInterpolatedColourTakes)
{
    // alpha0 = 255, alpha1 = 0, so the six interpolated values are sevenths; every index appears.
    const uint8_t block[16] = {
        0xFF, 0x00,
        0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA,        // indices 0..7 twice, three bits each
        0xFF, 0xFF, 0xFF, 0xFF,                    // a flat colour half
        0x00, 0x00, 0x00, 0x00
    };
    // Block (0,0) of a 4x4 surface: the dither threshold is kDitherThreshold[y & 3][x & 3].
    const std::vector<uint8_t> decoded = DxtUtil::DecompressDxt5(
        block, sizeof(block), 4, 4, CNA::Internal::Graphics::DxtEndpointExpansion::D3dx);

    // 255 and 0 are the endpoints; the rest are 255*(7-i)/7 narrowed by the dither at that texel.
    // These sixteen bytes are read off the genuine pipeline's own answer -- the block at (18, 6)
    // of `tests/assets/xna40/texture/dxt5_alpha_table.dds` in
    // `tests/reference/xna40/differential/texture_dxt5_alpha_table.xnb` -- rather than recomputed
    // here, so the test asserts the measurement and not the formula fitted to it. The dither
    // depends on the texel's position within its block and not on the block's position, which is
    // why one block at (0, 0) says what one block at (18, 6) says.
    static const uint8_t kExpected[16] = {
        255,   0, 219, 182,
        146, 109,  73,  36,
        255,   0, 219, 182,
        146, 109,  73,  36
    };
    for (int texel = 0; texel < 16; ++texel)
    {
        EXPECT_EQ(decoded[texel * 4 + 3], kExpected[texel])
            << "texel " << texel << " (x " << (texel % 4) << ", y " << (texel / 4) << ")";
    }

    // Without the D3DX expansion nothing dithers: the plain decoder keeps the truncated quotient,
    // which is what every renderer's own upload path has always seen.
    const std::vector<uint8_t> plain = DxtUtil::DecompressDxt5(
        block, sizeof(block), 4, 4, CNA::Internal::Graphics::DxtEndpointExpansion::Hardware);
    EXPECT_EQ(plain[2 * 4 + 3], static_cast<uint8_t>((6 * 255 + 0 * 0) / 7));
}
