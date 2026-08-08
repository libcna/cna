#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "CNA/Internal/Backends/Glide/GlidePrimitiveClip.hpp"

using CNA::Internal::Backends::Glide::ClipGlidePolygonToFrustum;
using CNA::Internal::Backends::Glide::ClipGlideSegmentToFrustum;
using CNA::Internal::Backends::Glide::GlideClipVertex;
using CNA::Internal::Backends::Glide::HasFiniteGlideClipCoordinates;
using CNA::Internal::Backends::Glide::IsGlidePointInsideFrustum;
using CNA::Internal::Backends::Glide::kGlideMinimumPositiveClipW;

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

namespace
{
    void ExpectEveryVertexIsPerspectiveDividable(const std::vector<GlideClipVertex>& polygon)
    {
        for (const GlideClipVertex& vertex : polygon)
        {
            EXPECT_TRUE(HasFiniteGlideClipCoordinates(vertex));
            EXPECT_GE(vertex.clipW, kGlideMinimumPositiveClipW);
        }
    }
} // namespace

TEST(GlidePrimitiveClipTest, PolygonClipsAnExactZeroWVertexAtTheEyePlane)
{
    // With clipX == clipY == clipZ == 0, this vertex sits exactly on the boundary of all six
    // nominal frustum planes (every "distance" evaluates to 0, which counts as inside), so only
    // the added positive-W margin plane can reject it. Before that plane existed this vertex
    // reached the perspective divide with W == 0.
    const std::vector<GlideClipVertex> polygon = {
        {-0.2f, -0.2f, 0.5f, 1.0f, 0.0f, 100.0f, 0.0f, 200.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f, 200.0f, 100.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {0.2f, -0.2f, 0.5f, 1.0f, 100.0f, 100.0f, 0.0f, 100.0f, 0.5f, 0.5f}};

    const auto clipped = ClipGlidePolygonToFrustum(polygon);

    // The original apex sits exactly at clipW == 0, so clipping it away must replace it with two
    // interpolated vertices (one per adjacent edge), leaving the two safely-inside corners intact.
    ASSERT_EQ(clipped.size(), 4u);
    ExpectEveryVertexIsPerspectiveDividable(clipped);
}

TEST(GlidePrimitiveClipTest, PolygonClipsABelowEpsilonPositiveWVertex)
{
    // A strictly positive W that is still below the margin is exactly as undividable as W == 0
    // and must be rejected the same way.
    const std::vector<GlideClipVertex> polygon = {
        {-0.2f, -0.2f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.000005f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        {0.2f, -0.2f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f}};

    const auto clipped = ClipGlidePolygonToFrustum(polygon);

    ASSERT_GE(clipped.size(), 3u);
    ExpectEveryVertexIsPerspectiveDividable(clipped);
}

TEST(GlidePrimitiveClipTest, PolygonCrossingTheEyePlaneKeepsOnlyDividableVertices)
{
    // A vertex clearly behind the eye (negative W) already fails the ordinary side planes when
    // clipX/clipY are 0 (they require -W <= 0 <= W, impossible for W < 0). This confirms the
    // composed seven-plane clip still produces a fully finite, dividable result once the new
    // plane is added alongside the pre-existing six.
    const std::vector<GlideClipVertex> polygon = {
        {0.0f, 0.0f, 0.5f, 1.0f, 10.0f, 20.0f, 30.0f, 40.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, -1.0f, 50.0f, 60.0f, 70.0f, 80.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.5f, 1.0f, 90.0f, 100.0f, 110.0f, 120.0f, 0.5f, 0.5f}};

    const auto clipped = ClipGlidePolygonToFrustum(polygon);

    ASSERT_GE(clipped.size(), 3u);
    ExpectEveryVertexIsPerspectiveDividable(clipped);
}

TEST(GlidePrimitiveClipTest, PolygonFullyBehindTheEyePlaneIsEntirelyRejected)
{
    const std::vector<GlideClipVertex> polygon = {
        {0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};

    const auto clipped = ClipGlidePolygonToFrustum(polygon);

    EXPECT_TRUE(clipped.empty());
}
