#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideTextureCoordinate.hpp"

using CNA::Internal::Backends::Glide::GlideNativeTextureCoordinateScale;
using CNA::Internal::Backends::Glide::kGlideTextureCoordinateFullRepeat;

TEST(GlideTextureCoordinateTest, SquareTileMapsOneFullTexelSpanToTheFullRepeatRange)
{
    const float scale = GlideNativeTextureCoordinateScale(128, 128);

    EXPECT_FLOAT_EQ(128.0f * scale, kGlideTextureCoordinateFullRepeat);
    EXPECT_FLOAT_EQ(64.0f * scale, kGlideTextureCoordinateFullRepeat * 0.5f);
}

TEST(GlideTextureCoordinateTest, WideTileCompressesTheShortAxisByItsAspectRatio)
{
    // 256x128 (2:1). The long (width) axis spans the full 256-unit repeat; the short (height)
    // axis, having half as many texels, must span exactly half that native range.
    const float scale = GlideNativeTextureCoordinateScale(256, 128);

    EXPECT_FLOAT_EQ(256.0f * scale, kGlideTextureCoordinateFullRepeat);
    EXPECT_FLOAT_EQ(128.0f * scale, kGlideTextureCoordinateFullRepeat * 0.5f);
}

TEST(GlideTextureCoordinateTest, TallTileCompressesTheShortAxisByItsAspectRatio)
{
    // 64x256 (1:4). The long (height) axis spans the full 256-unit repeat; the short (width)
    // axis, having a quarter as many texels, must span exactly a quarter of that native range.
    const float scale = GlideNativeTextureCoordinateScale(64, 256);

    EXPECT_FLOAT_EQ(256.0f * scale, kGlideTextureCoordinateFullRepeat);
    EXPECT_FLOAT_EQ(64.0f * scale, kGlideTextureCoordinateFullRepeat * 0.25f);
}

TEST(GlideTextureCoordinateTest, TheSameSharedScaleAppliesRegardlessOfActualTexelCount)
{
    // A tiny 2x2 tile and a large 128x128 tile must both map their full width to exactly the
    // 256-unit repeat: the "0..256 per repeat" convention is independent of physical texel count.
    EXPECT_FLOAT_EQ(2.0f * GlideNativeTextureCoordinateScale(2, 2), kGlideTextureCoordinateFullRepeat);
    EXPECT_FLOAT_EQ(128.0f * GlideNativeTextureCoordinateScale(128, 128), kGlideTextureCoordinateFullRepeat);
}

TEST(GlideTextureCoordinateTest, RejectsNonPositiveDimensions)
{
    EXPECT_THROW(static_cast<void>(GlideNativeTextureCoordinateScale(0, 64)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(GlideNativeTextureCoordinateScale(64, 0)), std::runtime_error);
}
