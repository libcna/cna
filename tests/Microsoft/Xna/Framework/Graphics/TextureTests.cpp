// SPDX-License-Identifier: MS-PL
// Task 282: Texture::GetBlockSizeSquaredEXT / GetFormatSizeEXT — canonical per-format CPU size.
//
// Both are real FNA API methods (Microsoft.Xna.Framework.Graphics.Texture, region
// "Static SurfaceFormat Size Methods" in Texture.cs) ported verbatim, including the exact byte
// sizes and block-size grouping per format. Values verified line-by-line against FNA's switch
// statements. `Texture` itself is abstract, so these static methods are exercised directly
// without needing a concrete subclass instance.

#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;

// -----------------------------------------------------------------------
// GetBlockSizeSquaredEXT — 16 for the 6 block-compressed formats, 1 for everything else
// -----------------------------------------------------------------------

TEST(TextureTest, GetBlockSizeSquaredEXT_CompressedFormatsAre16)
{
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Dxt1), 16);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Dxt3), 16);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Dxt5), 16);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Dxt5SrgbEXT), 16);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Bc7EXT), 16);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Bc7SrgbEXT), 16);
}

TEST(TextureTest, GetBlockSizeSquaredEXT_UncompressedFormatsAre1)
{
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Alpha8), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Bgr565), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Bgra4444), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Bgra5551), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::HalfSingle), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::NormalizedByte2), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Color), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Single), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Rg32), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::HalfVector2), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::NormalizedByte4), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Rgba1010102), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::ColorBgraEXT), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::ColorSrgbEXT), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::HalfVector4), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Rgba64), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Vector2), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::HdrBlendable), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::Vector4), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::ByteEXT), 1);
    EXPECT_EQ(Texture::GetBlockSizeSquaredEXT(SurfaceFormat::UShortEXT), 1);
}

TEST(TextureTest, GetBlockSizeSquaredEXT_InvalidValueThrowsOutOfRange)
{
    EXPECT_THROW(Texture::GetBlockSizeSquaredEXT(static_cast<SurfaceFormat>(27)), std::out_of_range);
    EXPECT_THROW(Texture::GetBlockSizeSquaredEXT(static_cast<SurfaceFormat>(-1)), std::out_of_range);
}

// -----------------------------------------------------------------------
// GetFormatSizeEXT — byte size of one block (compressed) or one texel (uncompressed)
// -----------------------------------------------------------------------

TEST(TextureTest, GetFormatSizeEXT_Dxt1Is8)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Dxt1), 8);
}

TEST(TextureTest, GetFormatSizeEXT_16ByteBlockFormats)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Dxt3), 16);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Dxt5), 16);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Dxt5SrgbEXT), 16);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Bc7EXT), 16);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Bc7SrgbEXT), 16);
}

TEST(TextureTest, GetFormatSizeEXT_1ByteFormats)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Alpha8), 1);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::ByteEXT), 1);
}

TEST(TextureTest, GetFormatSizeEXT_2ByteFormats)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Bgr565), 2);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Bgra4444), 2);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Bgra5551), 2);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::HalfSingle), 2);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::NormalizedByte2), 2);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::UShortEXT), 2);
}

TEST(TextureTest, GetFormatSizeEXT_4ByteFormats)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Color), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Single), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Rg32), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::HalfVector2), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::NormalizedByte4), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Rgba1010102), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::ColorBgraEXT), 4);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::ColorSrgbEXT), 4);
}

TEST(TextureTest, GetFormatSizeEXT_8ByteFormats)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::HalfVector4), 8);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Rgba64), 8);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Vector2), 8);
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::HdrBlendable), 8);
}

TEST(TextureTest, GetFormatSizeEXT_Vector4Is16)
{
    EXPECT_EQ(Texture::GetFormatSizeEXT(SurfaceFormat::Vector4), 16);
}

TEST(TextureTest, GetFormatSizeEXT_InvalidValueThrowsOutOfRange)
{
    EXPECT_THROW(Texture::GetFormatSizeEXT(static_cast<SurfaceFormat>(27)), std::out_of_range);
    EXPECT_THROW(Texture::GetFormatSizeEXT(static_cast<SurfaceFormat>(-1)), std::out_of_range);
}
