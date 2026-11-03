// SPDX-License-Identifier: MS-PL
//
// FNA3D-19: GTest coverage for the FNA3D renderer's transfer byte arithmetic.
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

// RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include <array>
#include <limits>
#include <stdexcept>
#include "System/ArgumentOutOfRangeException.hpp"

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


TEST(Fna3dSurfaceFormatTests, EveryFormatUsesItsSpecifiedTexelOrBlockSize)
{
    // Oracle: FNA Texture.GetFormatSize/GetBlockSizeSquared and SurfaceFormat bit widths.
    // DXT1 = 8 bytes per 4x4; DXT3/5 and BC7 = 16. Linear sizes are bits/8.
    struct Case { SurfaceFormat format; int bytes; bool compressed; };
    const std::array cases{
        Case{SurfaceFormat::Color, 4, false},
        Case{SurfaceFormat::Bgr565, 2, false},
        Case{SurfaceFormat::Bgra5551, 2, false},
        Case{SurfaceFormat::Bgra4444, 2, false},
        Case{SurfaceFormat::Dxt1, 8, true},
        Case{SurfaceFormat::Dxt3, 16, true},
        Case{SurfaceFormat::Dxt5, 16, true},
        Case{SurfaceFormat::NormalizedByte2, 2, false},
        Case{SurfaceFormat::NormalizedByte4, 4, false},
        Case{SurfaceFormat::Rgba1010102, 4, false},
        Case{SurfaceFormat::Rg32, 4, false},
        Case{SurfaceFormat::Rgba64, 8, false},
        Case{SurfaceFormat::Alpha8, 1, false},
        Case{SurfaceFormat::Single, 4, false},
        Case{SurfaceFormat::Vector2, 8, false},
        Case{SurfaceFormat::Vector4, 16, false},
        Case{SurfaceFormat::HalfSingle, 2, false},
        Case{SurfaceFormat::HalfVector2, 4, false},
        Case{SurfaceFormat::HalfVector4, 8, false},
        Case{SurfaceFormat::HdrBlendable, 8, false},
        Case{SurfaceFormat::ColorBgraEXT, 4, false},
        Case{SurfaceFormat::ColorSrgbEXT, 4, false},
        Case{SurfaceFormat::Dxt5SrgbEXT, 16, true},
        Case{SurfaceFormat::Bc7EXT, 16, true},
        Case{SurfaceFormat::Bc7SrgbEXT, 16, true},
        Case{SurfaceFormat::ByteEXT, 1, false},
        Case{SurfaceFormat::UShortEXT, 2, false},
    };
    for (const auto& [format, bytes, compressed] : cases)
    {
        SCOPED_TRACE(Ordinal(format));
        EXPECT_EQ(IsBlockCompressedFormat(Ordinal(format)), compressed);
        EXPECT_EQ(FormatUnitByteCount(Ordinal(format)), bytes);
        // 5x7 contains 35 texels or ceil(5/4)*ceil(7/4) = 4 compressed blocks.
        EXPECT_EQ(FormatRowByteCount(Ordinal(format), 5), (compressed ? 2 : 5) * bytes);
        EXPECT_EQ(FormatRegionByteCount(Ordinal(format), 5, 7), (compressed ? 4 : 35) * bytes);
        EXPECT_EQ(FormatRegionByteCount(Ordinal(format), 1, 1), bytes);
    }
}

TEST(Fna3dSurfaceFormatTests, TransferLengthsKeepTheLastRepresentableValue)
{
    // Oracle: FNA3D transfer dataLength is int32; 2^31-1 is its positive ceiling.
    constexpr int maximum = std::numeric_limits<int>::max();
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Alpha8), maximum), maximum);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Color), 536870911), 2147483644);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Color), 536870912), 0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Alpha8), 46340, 46340), 2147395600);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Alpha8), 46341, 46341), 0);
    // 1073741820 texels -> 268435455 blocks * 8 bytes = 2147483640.
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt1), 1073741820), 2147483640);
    EXPECT_EQ(FormatRowByteCount(Ordinal(SurfaceFormat::Dxt1), 1073741821), 0);
}

TEST(Fna3dSurfaceFormatTests, InvalidFormatsAreRefusedBeforeRegionTransfer)
{
    // Oracle: Texture format helpers throw for unknown formats; noexcept region validation
    // converts this to false so callers cannot submit an unrecognised native format.
    for (int invalid : {-1, 27, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        EXPECT_THROW((void) FormatUnitByteCount(invalid), System::ArgumentOutOfRangeException);
        EXPECT_THROW((void) IsBlockCompressedFormat(invalid), System::ArgumentOutOfRangeException);
        EXPECT_THROW((void) FormatRowByteCount(invalid, 4), System::ArgumentOutOfRangeException);
        EXPECT_THROW((void) FormatRegionByteCount(invalid, 4, 4), System::ArgumentOutOfRangeException);
        EXPECT_FALSE(IsValidTextureRegion2DForFormat(invalid, 8, 8, 0, 0, 4, 4));
    }
}

TEST(Fna3dSurfaceFormatTests, CompressedEdgeExceptionsAreIndependentOnBothAxes)
{
    // Oracle: 4x4 blocks must start on block boundaries; only mip edges may end mid-block.
    for (SurfaceFormat format : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
                                SurfaceFormat::Dxt5SrgbEXT, SurfaceFormat::Bc7EXT,
                                SurfaceFormat::Bc7SrgbEXT})
    {
        SCOPED_TRACE(Ordinal(format));
        EXPECT_TRUE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 4, 0, 6, 4));
        EXPECT_TRUE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 0, 4, 4, 6));
        EXPECT_FALSE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 0, 0, 6, 4));
        EXPECT_FALSE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 0, 0, 4, 6));
        EXPECT_FALSE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 6, 4, 4, 6));
        EXPECT_FALSE(IsValidTextureRegion2DForFormat(Ordinal(format), 10, 10, 4, 6, 6, 4));
        for (int size : {1, 2, 3})
            EXPECT_TRUE(IsValidTextureRegion2DForFormat(Ordinal(format), size, size, 0, 0, size, size));
    }
}

TEST(Fna3dSurfaceFormatTests, RegionBoundsHandleExtremeCoordinatesWithoutWrapping)
{
    // Oracle: nonempty [origin, origin+extent) must be contained on each axis.
    constexpr int maximum = std::numeric_limits<int>::max();
    EXPECT_TRUE(IsValidTextureRegion2D(maximum, maximum, maximum - 1, maximum - 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(maximum, maximum, maximum, 0, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 8, maximum, maximum, maximum, maximum));
    EXPECT_FALSE(IsValidTextureRegion2D(0, 8, 0, 0, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 0, 0, 0, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 8, 0, -1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion2D(8, 8, 0, 0, 1, -1));
    EXPECT_TRUE(IsValidTextureRegion3D(8, 8, maximum, 0, 0, maximum - 1, 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 8, maximum, 0, 0, maximum, 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 8, 0, 0, 0, 0, 1, 1, 1));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 8, 8, 0, 0, 0, 1, 1, 0));
    EXPECT_FALSE(IsValidTextureRegion3D(8, 8, 8, 0, 0, 0, 1, 1, maximum));
}


TEST(Fna3dSurfaceFormatTests, RegionByteCountRejectsOverflowBeforeSigned64Product)
{
    // Hand-derived oracle: (2^30+32768)*(2^30-32768+1) = 2^60+32768 texels.
    // Vector4 uses 16 bytes/texel, so the mathematical size is 2^64+524288 bytes,
    // not the small positive 524288 produced by an overflowing signed 64-bit product.
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Vector4), 1073774592, 1073709057), 0);
    EXPECT_EQ(FormatRegionByteCount(Ordinal(SurfaceFormat::Vector4), 1073709057, 1073774592), 0);
}

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
