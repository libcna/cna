#include <gtest/gtest.h>

#include <limits>

#include "CNA/Internal/Backends/Glide/GlidePrimitiveClip.hpp"

using CNA::Internal::Backends::Glide::ClipGlideSegmentToFrustum;
using CNA::Internal::Backends::Glide::GlideClipVertex;
using CNA::Internal::Backends::Glide::IsGlidePointInsideFrustum;

TEST(GlidePrimitiveClipTest, PointRequiresFinitePositiveHomogeneousClipCoordinates)
{
    EXPECT_TRUE(IsGlidePointInsideFrustum({0.0f, 0.0f, 0.5f, 1.0f}));
    EXPECT_TRUE(IsGlidePointInsideFrustum({1.0f, -1.0f, 1.0f, 1.0f}));
    EXPECT_FALSE(IsGlidePointInsideFrustum({1.01f, 0.0f, 0.5f, 1.0f}));
    EXPECT_FALSE(IsGlidePointInsideFrustum({0.0f, 0.0f, -0.01f, 1.0f}));
    EXPECT_FALSE(IsGlidePointInsideFrustum({0.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(IsGlidePointInsideFrustum(
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.5f, 1.0f}));
}

TEST(GlidePrimitiveClipTest, SegmentClipsAgainstTheSidePlaneAndInterpolatesEveryAttribute)
{
    GlideClipVertex first{-2.0f, 0.0f, 0.5f, 1.0f, 0.0f, 10.0f, 20.0f, 30.0f, 0.0f, 0.0f};
    GlideClipVertex second{0.0f, 0.0f, 0.5f, 1.0f, 200.0f, 30.0f, 40.0f, 50.0f, 1.0f, 1.0f};

    const auto clipped = ClipGlideSegmentToFrustum(first, second);

    ASSERT_TRUE(clipped.has_value());
    EXPECT_FLOAT_EQ(clipped->first.clipX, -1.0f);
    EXPECT_FLOAT_EQ(clipped->first.r, 100.0f);
    EXPECT_FLOAT_EQ(clipped->first.g, 20.0f);
    EXPECT_FLOAT_EQ(clipped->first.u, 0.5f);
    EXPECT_FLOAT_EQ(clipped->first.v, 0.5f);
    EXPECT_FLOAT_EQ(clipped->second.clipX, 0.0f);
}

TEST(GlidePrimitiveClipTest, SegmentClipsAtNearPlaneAndRejectsAWhollyOutsideSegment)
{
    GlideClipVertex first{0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    GlideClipVertex second{0.0f, 0.0f, 1.0f, 1.0f, 100.0f, 100.0f, 100.0f, 100.0f, 1.0f, 1.0f};

    const auto clipped = ClipGlideSegmentToFrustum(first, second);

    ASSERT_TRUE(clipped.has_value());
    EXPECT_FLOAT_EQ(clipped->first.clipZ, 0.0f);
    EXPECT_FLOAT_EQ(clipped->first.r, 50.0f);
    EXPECT_FLOAT_EQ(clipped->first.u, 0.5f);
    EXPECT_FALSE(ClipGlideSegmentToFrustum(
        {-3.0f, 0.0f, 0.5f, 1.0f}, {-2.0f, 0.0f, 0.5f, 1.0f}).has_value());
}

TEST(GlidePrimitiveClipTest, SegmentCrossingEyePlaneKeepsItsEndpointsPerspectiveDividable)
{
    const auto clipped = ClipGlideSegmentToFrustum(
        {0.0f, 0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.5f, 1.0f});

    ASSERT_TRUE(clipped.has_value());
    EXPECT_GT(clipped->first.clipW, 0.0f);
    EXPECT_GT(clipped->second.clipW, 0.0f);
}
