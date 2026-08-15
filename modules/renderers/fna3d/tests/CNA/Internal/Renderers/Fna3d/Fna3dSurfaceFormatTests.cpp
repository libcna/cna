// SPDX-License-Identifier: MS-PL
//
// plan_fna3d.md FNA3D-19: GTest coverage for the FNA3D renderer's transfer byte arithmetic.
//
// This is the arithmetic that decides how many bytes reach FNA3D_SetTextureData2D and come back
// from FNA3D_GetTextureData2D. Before it existed the renderer assumed `width * height * 4` for
// every format, which reads past the end of every block-compressed upload -- a real out-of-bounds
// access, not a cosmetic mis-size, because raw compressed blocks travel through the same
// RGBA-named transfer methods (SKIA-140/141's convention).
//
// The reachable entry point is the renderer contract, not the XNA layer: `ImageData::surfaceFormat`
// carries an arbitrary SurfaceFormat ordinal into `CreateTexture`, whereas the shared
// `Texture::ValidateFormat` still admits only `SurfaceFormat::Color` for every renderer but Skia,
// so no public `Texture2D` under FNA3D is compressed today.
//
// The padded-tail cases below are the ones a naive `ceil` gets wrong: a 5-texel-wide level still
// occupies two full block columns, and shorting the count by that partial tail block is exactly
// the under-read that corrupts the last block row.
#include <gtest/gtest.h>

#include <limits>

#if defined(CNA_RENDERER_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dSurfaceFormats.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

namespace
{
    using namespace CNA::Internal::Renderers::Fna3d;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    constexpr int Ordinal(SurfaceFormat format) { return static_cast<int>(format); }
}

TEST(Fna3dSurfaceFormatTests, CompressedFormatsAreRecognisedAndLinearOnesAreNot)
{
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Dxt1)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Dxt3)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Dxt5)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Dxt5SrgbEXT)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Bc7EXT)));
    EXPECT_TRUE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Bc7SrgbEXT)));

    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Color)));
    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Bgr565)));
    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Vector4)));
    EXPECT_FALSE(IsBlockCompressedFormat(Ordinal(SurfaceFormat::Alpha8)));
}

TEST(Fna3dSurfaceFormatTests, LinearRegionsAreWidthTimesHeightTimesTexelSize)
{
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Color), 8, 8), 8 * 8 * 4);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Alpha8), 8, 8), 8 * 8 * 1);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Bgr565), 8, 8), 8 * 8 * 2);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Vector4), 4, 4), 4 * 4 * 16);
}

TEST(Fna3dSurfaceFormatTests, CompressedRegionsAreCountedInWholeBlocks)
{
    // Dxt1 is 8 bytes per 4x4 block; Dxt3/Dxt5/Bc7 are 16.
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt1), 8, 8), 2 * 2 * 8);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 8, 8), 2 * 2 * 16);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Bc7EXT), 16, 16), 4 * 4 * 16);
}

TEST(Fna3dSurfaceFormatTests, PartialTailBlocksAreCountedInFull)
{
    // The whole point: 5 texels still need two block columns, 1 texel still needs one block.
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 5, 5), 2 * 2 * 16);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 1, 1), 1 * 1 * 16);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 6, 6), 2 * 2 * 16);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt1), 3, 9), 1 * 3 * 8);

    // A compressed level is never smaller than one block, which is what an RGBA-shaped
    // `width * height * 4` gets wrong in the other direction for a 1x1 mip tail.
    EXPECT_GE(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 1, 1), 16);
}

TEST(Fna3dSurfaceFormatTests, RowPitchIsOneBlockRowForCompressedFormats)
{
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Color), 8), 8 * 4);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt5), 8), 2 * 16);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt5), 5), 2 * 16);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt1), 16), 4 * 8);
}

TEST(Fna3dSurfaceFormatTests, RowPitchTimesBlockRowsEqualsTheRegionCount)
{
    // The two helpers must agree, because UpdatePixels uses the row pitch to repack and the
    // region count to size the upload: a disagreement is a buffer overrun in exactly that path.
    for (const SurfaceFormat format : { SurfaceFormat::Color, SurfaceFormat::Dxt1,
                                        SurfaceFormat::Dxt5, SurfaceFormat::Bc7EXT })
    {
        for (const int width : { 1, 3, 4, 5, 8, 13, 16 })
        {
            for (const int height : { 1, 3, 4, 5, 8, 13, 16 })
            {
                const int rows = IsBlockCompressedFormat(Ordinal(format)) ? (height + 3) / 4
                                                                          : height;
                EXPECT_EQ(FormatRowByteCount(Ordinal(format), width) * rows,
                          FormatRegionByteCount(Ordinal(format), width, height))
                    << "format=" << Ordinal(format) << " " << width << "x" << height;
            }
        }
    }
}

TEST(Fna3dSurfaceFormatTests, NonPositiveExtentsCountZeroRatherThanNegative)
{
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Color), 0, 8), 0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), 8, 0), 0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5), -4, -4), 0);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt5), 0), 0);
}

TEST(Fna3dSurfaceFormatTests, ByteArithmeticRejectsSignedIntOverflow)
{
    // FNA3D's transfer-length parameter is signed int32. A wrapped positive count would make a
    // native upload/readback smaller than the region it is asked to cover, so helpers return zero
    // rather than wrapping.
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Color), std::numeric_limits<int>::max()),
              0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Color), 1 << 30, 4), 0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Dxt5),
                                    std::numeric_limits<int>::max(), 4),
              0);
}

TEST(Fna3dSurfaceFormatTests, TransferRegionsMustFitTheirMipExtent)
{
    EXPECT_TRUE(IsValidTextureRegion2D(8, 4, 0, 0, 8, 4));
    EXPECT_TRUE(IsValidTextureRegion2D(8, 4, 7, 3, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 4, -1, 0, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 4, 7, 3, 2, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 4, 0, 4, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 4, 0, 0, 0, 1));

    EXPECT_TRUE(IsValidTextureRegion3D(8, 4, 2, 7, 3, 1, 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 4, 2, 0, 0, -1, 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 4, 2, 0, 0, 1, 1, 1, 2));
}

TEST(Fna3dSurfaceFormatTests, CompressedTransferRegionsRespectBlockBoundaries)
{
    const int color = Ordinal(SurfaceFormat::Color);
    const int dxt5 = Ordinal(SurfaceFormat::Dxt5);

    EXPECT_TRUE(IsValidTextureRegion2DForFormat(color, 10, 10, 1, 1, 3, 3));
    EXPECT_TRUE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 0, 0, 4, 4));
    EXPECT_TRUE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 4, 4, 6, 6));
    EXPECT_TRUE(IsValidTextureRegion2DForFormat(dxt5, 6, 6, 0, 0, 6, 6));

    EXPECT_FALSE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 1, 0, 4, 4));
    EXPECT_FALSE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 0, 2, 4, 4));
    EXPECT_FALSE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 0, 0, 6, 4));
    EXPECT_FALSE(IsValidTextureRegion2DForFormat(dxt5, 10, 10, 0, 0, 4, 6));
}

#endif // CNA_RENDERER_FNA3D
