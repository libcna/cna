#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideTextureFormat.hpp"

using CNA::Internal::Backends::Glide::ClassifyGlideArgb4444AlphaCoverage;
using CNA::Internal::Backends::Glide::CombineGlideTextureAlphaClass;
using CNA::Internal::Backends::Glide::GlideArgb4444ToArgb1555;
using CNA::Internal::Backends::Glide::GlideArgb4444ToRgb565;
using CNA::Internal::Backends::Glide::GlideTextureAlphaClass;

namespace
{
    // ARGB4444 packing: bits 15-12 A, 11-8 R, 7-4 G, 3-0 B (matches RgbaToGlideArgb4444).
    constexpr std::uint16_t MakeArgb4444(int a, int r, int g, int b)
    {
        return static_cast<std::uint16_t>((a << 12) | (r << 8) | (g << 4) | b);
    }
} // namespace

TEST(GlideTextureFormatTest, ClassifiesAllFullyOpaqueTexelsAsOpaque)
{
    const std::vector<std::uint16_t> texels{
        MakeArgb4444(0xF, 0x1, 0x2, 0x3), MakeArgb4444(0xF, 0x4, 0x5, 0x6)};

    EXPECT_EQ(ClassifyGlideArgb4444AlphaCoverage(texels), GlideTextureAlphaClass::Opaque);
}

TEST(GlideTextureFormatTest, ClassifiesAMixOfFullyTransparentAndOpaqueAsBinary)
{
    const std::vector<std::uint16_t> texels{
        MakeArgb4444(0xF, 0x1, 0x2, 0x3), MakeArgb4444(0x0, 0x0, 0x0, 0x0)};

    EXPECT_EQ(ClassifyGlideArgb4444AlphaCoverage(texels), GlideTextureAlphaClass::Binary);
}

TEST(GlideTextureFormatTest, ClassifiesAllFullyTransparentAsBinary)
{
    const std::vector<std::uint16_t> texels{
        MakeArgb4444(0x0, 0x1, 0x2, 0x3), MakeArgb4444(0x0, 0x4, 0x5, 0x6)};

    EXPECT_EQ(ClassifyGlideArgb4444AlphaCoverage(texels), GlideTextureAlphaClass::Binary);
}

TEST(GlideTextureFormatTest, ASingleIntermediateAlphaTexelForcesFractional)
{
    const std::vector<std::uint16_t> texels{
        MakeArgb4444(0xF, 0x1, 0x2, 0x3), MakeArgb4444(0x8, 0x0, 0x0, 0x0),
        MakeArgb4444(0x0, 0x0, 0x0, 0x0)};

    EXPECT_EQ(ClassifyGlideArgb4444AlphaCoverage(texels), GlideTextureAlphaClass::Fractional);
}

TEST(GlideTextureFormatTest, EmptyLevelIsVacuouslyOpaque)
{
    EXPECT_EQ(ClassifyGlideArgb4444AlphaCoverage({}), GlideTextureAlphaClass::Opaque);
}

TEST(GlideTextureFormatTest, CombineTakesTheWorstOfTwoClassifications)
{
    using Class = GlideTextureAlphaClass;
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Opaque, Class::Opaque), Class::Opaque);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Opaque, Class::Binary), Class::Binary);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Binary, Class::Opaque), Class::Binary);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Binary, Class::Binary), Class::Binary);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Opaque, Class::Fractional), Class::Fractional);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Fractional, Class::Binary), Class::Fractional);
    EXPECT_EQ(CombineGlideTextureAlphaClass(Class::Fractional, Class::Fractional), Class::Fractional);
}

TEST(GlideTextureFormatTest, Rgb565ConversionExpandsChannelsToTheirFullRangeAtTheExtremes)
{
    const std::uint16_t white = GlideArgb4444ToRgb565(MakeArgb4444(0xF, 0xF, 0xF, 0xF));
    EXPECT_EQ(white, 0xFFFFu);

    const std::uint16_t black = GlideArgb4444ToRgb565(MakeArgb4444(0xF, 0x0, 0x0, 0x0));
    EXPECT_EQ(black, 0x0000u);
}

TEST(GlideTextureFormatTest, Rgb565ConversionPlacesChannelsInTheDocumentedBitPositions)
{
    // Pure max-red input: R nibble 0xF, G/B 0.
    const std::uint16_t red = GlideArgb4444ToRgb565(MakeArgb4444(0xF, 0xF, 0x0, 0x0));
    EXPECT_EQ(red, static_cast<std::uint16_t>(0x1F << 11));

    const std::uint16_t green = GlideArgb4444ToRgb565(MakeArgb4444(0xF, 0x0, 0xF, 0x0));
    EXPECT_EQ(green, static_cast<std::uint16_t>(0x3F << 5));

    const std::uint16_t blue = GlideArgb4444ToRgb565(MakeArgb4444(0xF, 0x0, 0x0, 0xF));
    EXPECT_EQ(blue, 0x1Fu);
}

TEST(GlideTextureFormatTest, Argb1555ConversionSetsTheAlphaBitFromTheNibble)
{
    const std::uint16_t opaqueWhite = GlideArgb4444ToArgb1555(MakeArgb4444(0xF, 0xF, 0xF, 0xF));
    EXPECT_EQ(opaqueWhite, 0xFFFFu);

    const std::uint16_t transparentBlack = GlideArgb4444ToArgb1555(MakeArgb4444(0x0, 0x0, 0x0, 0x0));
    EXPECT_EQ(transparentBlack, 0x0000u);
}

TEST(GlideTextureFormatTest, Argb1555ConversionPlacesChannelsInTheDocumentedBitPositions)
{
    const std::uint16_t red = GlideArgb4444ToArgb1555(MakeArgb4444(0xF, 0xF, 0x0, 0x0));
    EXPECT_EQ(red, static_cast<std::uint16_t>((1u << 15) | (0x1F << 10)));

    const std::uint16_t green = GlideArgb4444ToArgb1555(MakeArgb4444(0xF, 0x0, 0xF, 0x0));
    EXPECT_EQ(green, static_cast<std::uint16_t>((1u << 15) | (0x1F << 5)));

    const std::uint16_t blue = GlideArgb4444ToArgb1555(MakeArgb4444(0xF, 0x0, 0x0, 0xF));
    EXPECT_EQ(blue, static_cast<std::uint16_t>((1u << 15) | 0x1Fu));
}
