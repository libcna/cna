#include <gtest/gtest.h>

#include <array>
#include <limits>

#include "CNA/Internal/Backends/Glide/GlideTextureMip.hpp"

using CNA::Internal::Backends::Glide::AddressGlideTextureTexel;
using CNA::Internal::Backends::Glide::BuildAddressedGlideArgb4444Mip;
using CNA::Internal::Backends::Glide::GlideMipLevelCountForDimensions;
using CNA::Internal::Backends::Glide::MapGlideTextureCoordinateToUnit;
using CNA::Internal::Backends::Glide::RgbaToGlideArgb4444;

namespace
{
    constexpr std::array<std::uint8_t, 12> kThreeTexels{
        0xff, 0x00, 0x00, 0xff,
        0x00, 0xff, 0x00, 0xff,
        0x00, 0x00, 0xff, 0xff};
}

TEST(GlideTextureMipTest, ConvertsRgba8ToNativeArgb4444)
{
    EXPECT_EQ(RgbaToGlideArgb4444(kThreeTexels.data()), 0xff00u);
    EXPECT_EQ(RgbaToGlideArgb4444(kThreeTexels.data() + 4), 0xf0f0u);
    EXPECT_EQ(RgbaToGlideArgb4444(kThreeTexels.data() + 8), 0xf00fu);
}

TEST(GlideTextureMipTest, CountsFullLogicalMipChains)
{
    EXPECT_EQ(GlideMipLevelCountForDimensions(1, 1), 1);
    EXPECT_EQ(GlideMipLevelCountForDimensions(2, 2), 2);
    EXPECT_EQ(GlideMipLevelCountForDimensions(3, 5), 3);
    EXPECT_EQ(GlideMipLevelCountForDimensions(16, 1), 5);
    EXPECT_THROW(static_cast<void>(GlideMipLevelCountForDimensions(0, 1)), std::runtime_error);
}

TEST(GlideTextureMipTest, MapsContinuousWrapClampAndMirrorCoordinates)
{
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(-0.25f, 0), 0.75f);
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(1.0f, 0), 0.0f);
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(-0.25f, 1), 0.0f);
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(1.25f, 1), 1.0f);
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(-0.25f, 2), 0.25f);
    EXPECT_FLOAT_EQ(MapGlideTextureCoordinateToUnit(1.25f, 2), 0.75f);
    EXPECT_THROW(static_cast<void>(MapGlideTextureCoordinateToUnit(
                     std::numeric_limits<float>::infinity(), 0)), std::runtime_error);
}

TEST(GlideTextureMipTest, ExpandsExplicitMipWithWrapPadding)
{
    const auto expanded = BuildAddressedGlideArgb4444Mip(kThreeTexels.data(), 3, 1, 4, 1, 0, 1);

    ASSERT_EQ(expanded.size(), 4u);
    EXPECT_EQ(expanded[0], 0xff00u);
    EXPECT_EQ(expanded[1], 0xf0f0u);
    EXPECT_EQ(expanded[2], 0xf00fu);
    EXPECT_EQ(expanded[3], 0xff00u);
}

TEST(GlideTextureMipTest, ExpandsExplicitMipWithClampAndMirrorPadding)
{
    const auto clamped = BuildAddressedGlideArgb4444Mip(kThreeTexels.data(), 3, 1, 4, 1, 1, 1);
    const auto mirrored = BuildAddressedGlideArgb4444Mip(kThreeTexels.data(), 3, 1, 4, 1, 2, 1);

    EXPECT_EQ(clamped[3], 0xf00fu);
    EXPECT_EQ(mirrored[3], 0xf00fu);
    EXPECT_EQ(AddressGlideTextureTexel(-1, 3, 0), 2);
    EXPECT_EQ(AddressGlideTextureTexel(-1, 3, 1), 0);
    EXPECT_EQ(AddressGlideTextureTexel(-1, 3, 2), 0);
}
