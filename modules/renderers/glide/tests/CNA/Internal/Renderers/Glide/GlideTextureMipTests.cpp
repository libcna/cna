#include <gtest/gtest.h>

#include <array>
#include <limits>

#include "CNA/Internal/Renderers/Glide/GlideTextureMip.hpp"

using CNA::Internal::Renderers::Glide::AddressGlideTextureTexel;
using CNA::Internal::Renderers::Glide::BuildAddressedGlideArgb4444Mip;
using CNA::Internal::Renderers::Glide::CopyGlideRgba8Rows;
using CNA::Internal::Renderers::Glide::GlideMipLevelCountForDimensions;
using CNA::Internal::Renderers::Glide::MapGlideTextureCoordinateToUnit;
using CNA::Internal::Renderers::Glide::RgbaToGlideArgb4444;

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

TEST(GlideTextureMipTest, CopiesNontrivialWidthFromBytePitchWithoutCopyingPadding)
{
    constexpr int kWidth = 3;
    constexpr int kHeight = 2;
    constexpr int kRowBytes = kWidth * 4;
    constexpr int kPitch = 16;
    const std::array<std::uint8_t, kPitch * kHeight> source{
        1, 2, 3, 4,   5, 6, 7, 8,   9, 10, 11, 12,   201, 202, 203, 204,
        21, 22, 23, 24,   25, 26, 27, 28,   29, 30, 31, 32,   211, 212, 213, 214};
    std::vector<std::uint8_t> destination(kRowBytes * kHeight, 0);

    CopyGlideRgba8Rows(destination, kWidth, kHeight, source.data(), kPitch);

    const std::vector<std::uint8_t> expected{
        1, 2, 3, 4,   5, 6, 7, 8,   9, 10, 11, 12,
        21, 22, 23, 24,   25, 26, 27, 28,   29, 30, 31, 32};
    EXPECT_EQ(destination, expected);
}

TEST(GlideTextureMipTest, RejectsPitchShorterThanOneRgba8Row)
{
    std::array<std::uint8_t, 24> source{};
    std::vector<std::uint8_t> destination(24, 0);
    EXPECT_THROW(CopyGlideRgba8Rows(destination, 3, 2, source.data(), 11),
                 std::runtime_error);
    EXPECT_THROW(CopyGlideRgba8Rows(destination, 3, 2, source.data(), -1),
                 std::runtime_error);
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
