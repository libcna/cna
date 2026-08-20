// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2041: the frustum cut into clusters, checked against trigonometry rather than
// against itself.
//
// The implementation gets a cluster's shape by unprojecting tile corners through the inverse of the
// projection matrix. Testing it with the same inverse would only prove the code runs, so the
// reference here is built the other way -- from the field of view and the aspect ratio, where the
// half-height at a distance is that distance times the tangent of half the vertical field of view.
// If the matrix convention were wrong, the two would disagree.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <stdexcept>

namespace {

using CNA::Graphics::ClusteredLightGrid;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

constexpr float kFieldOfView = 1.0471975512f;   // 60 degrees
constexpr float kAspect      = 16.0f / 9.0f;
constexpr float kNear        = 0.5f;
constexpr float kFar         = 200.0f;

Matrix TestProjection()
{
    return Matrix::CreatePerspectiveFieldOfView(kFieldOfView, kAspect, kNear, kFar);
}

// The reference: no matrices anywhere in it.
BoundingBox ReferenceBounds(const ClusteredLightGrid& grid, const int x, const int y,
                            const int slice)
{
    const float u0 = 2.0f * static_cast<float>(x)     / static_cast<float>(grid.getTilesX()) - 1.0f;
    const float u1 = 2.0f * static_cast<float>(x + 1) / static_cast<float>(grid.getTilesX()) - 1.0f;
    const float v0 = 2.0f * static_cast<float>(y)     / static_cast<float>(grid.getTilesY()) - 1.0f;
    const float v1 = 2.0f * static_cast<float>(y + 1) / static_cast<float>(grid.getTilesY()) - 1.0f;

    const float ratio = kFar / kNear;
    const float exponentNear = static_cast<float>(slice)     / static_cast<float>(grid.getSliceCount());
    const float exponentFar  = static_cast<float>(slice + 1) / static_cast<float>(grid.getSliceCount());
    const float dNear = kNear * std::pow(ratio, exponentNear);
    const float dFar  = kNear * std::pow(ratio, exponentFar);

    const float tangent = std::tan(kFieldOfView * 0.5f);

    Vector3 minimum(1e30f, 1e30f, 1e30f);
    Vector3 maximum(-1e30f, -1e30f, -1e30f);
    for (const float d : {dNear, dFar})
    {
        const float halfHeight = d * tangent;
        const float halfWidth  = halfHeight * kAspect;
        for (const float u : {u0, u1})
            for (const float v : {v0, v1})
            {
                const Vector3 p(u * halfWidth, v * halfHeight, -d);
                minimum.X = std::min(minimum.X, p.X);
                minimum.Y = std::min(minimum.Y, p.Y);
                minimum.Z = std::min(minimum.Z, p.Z);
                maximum.X = std::max(maximum.X, p.X);
                maximum.Y = std::max(maximum.Y, p.Y);
                maximum.Z = std::max(maximum.Z, p.Z);
            }
    }
    return BoundingBox(minimum, maximum);
}

TEST(ClusteredLightGridTest, TheDefaultGridIsSixteenByEightByTwentyFour)
{
    const ClusteredLightGrid grid;
    EXPECT_EQ(grid.getTilesX(), 16);
    EXPECT_EQ(grid.getTilesY(), 8);
    EXPECT_EQ(grid.getSliceCount(), 24);
    EXPECT_EQ(grid.getClusterCount(), 16 * 8 * 24);
    EXPECT_FALSE(grid.hasProjection()) << "a grid with no camera cannot have a shape yet";
}

TEST(ClusteredLightGridTest, ANonsensicalShapeIsRejected)
{
    EXPECT_THROW(ClusteredLightGrid(0, 8, 24), std::invalid_argument);
    EXPECT_THROW(ClusteredLightGrid(16, 0, 24), std::invalid_argument);
    EXPECT_THROW(ClusteredLightGrid(16, 8, 0), std::invalid_argument);
    EXPECT_THROW(ClusteredLightGrid(1000, 8, 24), std::invalid_argument);
    EXPECT_THROW(ClusteredLightGrid(16, 8, 1000), std::invalid_argument);
    EXPECT_NO_THROW(ClusteredLightGrid(1, 1, 1));
}

TEST(ClusteredLightGridTest, TheClusterIndexRunsXFastestAndIsABijection)
{
    // The order is stated in the header because a shader has to reproduce it. That makes it API,
    // so it is pinned rather than described: every cluster gets exactly one index, and the indices
    // fill the range with no gaps.
    const ClusteredLightGrid grid(4, 3, 5);
    EXPECT_EQ(grid.clusterIndex(0, 0, 0), 0);
    EXPECT_EQ(grid.clusterIndex(1, 0, 0), 1) << "x must be the fastest axis";
    EXPECT_EQ(grid.clusterIndex(0, 1, 0), 4);
    EXPECT_EQ(grid.clusterIndex(0, 0, 1), 12) << "a whole depth slice must be contiguous";
    EXPECT_EQ(grid.clusterIndex(3, 2, 4), 59);

    std::vector<int> seen(static_cast<std::size_t>(grid.getClusterCount()), 0);
    for (int slice = 0; slice < grid.getSliceCount(); ++slice)
        for (int y = 0; y < grid.getTilesY(); ++y)
            for (int x = 0; x < grid.getTilesX(); ++x)
                ++seen[static_cast<std::size_t>(grid.clusterIndex(x, y, slice))];
    for (const int count : seen) EXPECT_EQ(count, 1);

    EXPECT_THROW(grid.clusterIndex(4, 0, 0), std::out_of_range);
    EXPECT_THROW(grid.clusterIndex(0, 3, 0), std::out_of_range);
    EXPECT_THROW(grid.clusterIndex(0, 0, 5), std::out_of_range);
    EXPECT_THROW(grid.clusterIndex(-1, 0, 0), std::out_of_range);
}

TEST(ClusteredLightGridTest, AnUnusableCameraIsRejected)
{
    ClusteredLightGrid grid;
    const Matrix projection = TestProjection();
    EXPECT_THROW(grid.setProjection(projection, 0.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(grid.setProjection(projection, -1.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(grid.setProjection(projection, 100.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(grid.setProjection(projection, 100.0f, 1.0f), std::invalid_argument);
    EXPECT_THROW(grid.clusterBounds(0, 0, 0), std::runtime_error)
        << "a grid without a camera must refuse rather than answer a box of zeros";
}

TEST(ClusteredLightGridTest, SlicesAreSpacedExponentiallyAndMeetEndToEnd)
{
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    EXPECT_FLOAT_EQ(grid.sliceDistance(0), kNear);
    EXPECT_FLOAT_EQ(grid.sliceDistance(grid.getSliceCount()), kFar);

    // The defining property: every slice has the same ratio of far to near distance, which is what
    // makes the near ones thin. An evenly spaced grid would fail this at every slice but one.
    const float expected = std::pow(kFar / kNear, 1.0f / static_cast<float>(grid.getSliceCount()));
    for (int slice = 0; slice < grid.getSliceCount(); ++slice)
    {
        const float a = grid.sliceDistance(slice);
        const float b = grid.sliceDistance(slice + 1);
        EXPECT_GT(b, a) << "slice " << slice << " does not advance";
        EXPECT_NEAR(b / a, expected, 1e-4f) << "slice " << slice << " is not exponentially spaced";
    }

    // The near slices are thinner than the far ones, which is the whole point of the spacing.
    const float firstThickness = grid.sliceDistance(1) - grid.sliceDistance(0);
    const float lastThickness  = grid.sliceDistance(grid.getSliceCount()) -
                                 grid.sliceDistance(grid.getSliceCount() - 1);
    EXPECT_LT(firstThickness * 10.0f, lastThickness);

    EXPECT_THROW(grid.sliceDistance(-1), std::out_of_range);
    EXPECT_THROW(grid.sliceDistance(grid.getSliceCount() + 1), std::out_of_range);
}

TEST(ClusteredLightGridTest, ADistanceLandsInTheSliceThatContainsIt)
{
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    for (int slice = 0; slice < grid.getSliceCount(); ++slice)
    {
        // The midpoint of a slice, in the spacing's own terms rather than arithmetically, so the
        // sample cannot drift into a neighbour at the near end where slices are thin.
        const float middle = std::sqrt(grid.sliceDistance(slice) * grid.sliceDistance(slice + 1));
        EXPECT_EQ(grid.sliceForViewDistance(middle), slice) << "at distance " << middle;
    }

    // Outside the grid the answer is clamped rather than refused: geometry in front of the near
    // plane is still lit, and a shader has no way to ask a second time.
    EXPECT_EQ(grid.sliceForViewDistance(0.0f), 0);
    EXPECT_EQ(grid.sliceForViewDistance(kNear * 0.5f), 0);
    EXPECT_EQ(grid.sliceForViewDistance(-5.0f), 0);
    EXPECT_EQ(grid.sliceForViewDistance(kFar * 10.0f), grid.getSliceCount() - 1);
}

TEST(ClusteredLightGridTest, ClusterBoundsMatchTheTrigonometricReference)
{
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    for (const int slice : {0, 1, 7, 12, 23})
        for (const int y : {0, 3, 7})
            for (const int x : {0, 5, 8, 15})
            {
                const BoundingBox actual = grid.clusterBounds(x, y, slice);
                const BoundingBox expected = ReferenceBounds(grid, x, y, slice);
                const float tolerance = 1e-3f * std::max(1.0f, std::fabs(expected.Min.Z));
                EXPECT_NEAR(actual.Min.X, expected.Min.X, tolerance) << x << "," << y << "," << slice;
                EXPECT_NEAR(actual.Min.Y, expected.Min.Y, tolerance) << x << "," << y << "," << slice;
                EXPECT_NEAR(actual.Min.Z, expected.Min.Z, tolerance) << x << "," << y << "," << slice;
                EXPECT_NEAR(actual.Max.X, expected.Max.X, tolerance) << x << "," << y << "," << slice;
                EXPECT_NEAR(actual.Max.Y, expected.Max.Y, tolerance) << x << "," << y << "," << slice;
                EXPECT_NEAR(actual.Max.Z, expected.Max.Z, tolerance) << x << "," << y << "," << slice;
            }
}

TEST(ClusteredLightGridTest, NeighbouringClustersOverlapRatherThanLeaveAGap)
{
    // This test first asserted that neighbouring boxes share a face, and that was wrong in a way
    // worth keeping written down. Two clusters side by side share a frustum *plane*, not an
    // axis-aligned one: along a tile edge left of centre the box reaches furthest left at the far
    // distance and furthest right at the near one, so the left cluster's Max.X is taken at the near
    // distance and the right cluster's Min.X at the far distance, and the two are different
    // numbers. The boxes therefore overlap in a staircase. That is the correct behaviour for a
    // conservative bound -- what must never happen is a *gap*, which would drop a light that sits
    // between two clusters. So the property is coverage, not tiling.
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    const BoundingBox a = grid.clusterBounds(4, 3, 6);
    const BoundingBox right = grid.clusterBounds(5, 3, 6);
    const BoundingBox above = grid.clusterBounds(4, 4, 6);
    const BoundingBox behind = grid.clusterBounds(4, 3, 7);

    EXPECT_GE(a.Max.X, right.Min.X) << "a gap between horizontal neighbours";
    EXPECT_GE(a.Max.Y, above.Min.Y) << "a gap between vertical neighbours";

    // Depth is the one axis where the boxes do meet exactly, because a slice boundary is a plane
    // of constant view depth and nothing is being bounded conservatively.
    EXPECT_NEAR(a.Min.Z, behind.Max.Z, 1e-4f) << "a gap between depth neighbours";

    // And the grid as a whole covers the frustum: the outermost tiles reach the frustum edge at
    // the far plane, which is the number tan(fov/2) gives directly.
    const BoundingBox last = grid.clusterBounds(grid.getTilesX() - 1, grid.getTilesY() - 1,
                                                grid.getSliceCount() - 1);
    const float halfHeight = kFar * std::tan(kFieldOfView * 0.5f);
    EXPECT_NEAR(last.Max.Y, halfHeight, 1e-2f);
    EXPECT_NEAR(last.Max.X, halfHeight * kAspect, 1e-2f);
    EXPECT_NEAR(last.Min.Z, -kFar, 1e-2f);
}

TEST(ClusteredLightGridTest, EveryPointInTheFrustumLandsInsideItsOwnClustersBounds)
{
    // The property the assignment step in MOD-2042 will depend on: whatever cluster the tile and
    // slice arithmetic names for a point, that cluster's box has to actually contain the point.
    // A sign error in the unprojection, or NDC y pointing the other way, breaks this and nothing
    // else here would catch it.
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    const float tangent = std::tan(kFieldOfView * 0.5f);
    for (int slice = 0; slice < grid.getSliceCount(); slice += 3)
    {
        const float d = std::sqrt(grid.sliceDistance(slice) * grid.sliceDistance(slice + 1));
        EXPECT_EQ(grid.sliceForViewDistance(d), slice);
        for (int y = 0; y < grid.getTilesY(); ++y)
            for (int x = 0; x < grid.getTilesX(); ++x)
            {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) /
                                static_cast<float>(grid.getTilesX()) - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(grid.getTilesY()) - 1.0f;
                const Vector3 point(u * d * tangent * kAspect, v * d * tangent, -d);

                const BoundingBox box = grid.clusterBounds(x, y, slice);
                EXPECT_GE(point.X, box.Min.X - 1e-3f) << x << "," << y << "," << slice;
                EXPECT_LE(point.X, box.Max.X + 1e-3f) << x << "," << y << "," << slice;
                EXPECT_GE(point.Y, box.Min.Y - 1e-3f) << x << "," << y << "," << slice;
                EXPECT_LE(point.Y, box.Max.Y + 1e-3f) << x << "," << y << "," << slice;
                EXPECT_GE(point.Z, box.Min.Z - 1e-3f) << x << "," << y << "," << slice;
                EXPECT_LE(point.Z, box.Max.Z + 1e-3f) << x << "," << y << "," << slice;
            }
    }
}

TEST(ClusteredLightGridTest, ClusterBoundsGrowWithDistance)
{
    // The grid is a frustum, so a cluster's cross-section has to grow with distance. A grid that
    // was accidentally built in NDC would produce equal boxes and pass every other test here.
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);

    const BoundingBox nearCluster = grid.clusterBounds(8, 4, 0);
    const BoundingBox farCluster  = grid.clusterBounds(8, 4, 23);
    EXPECT_GT(farCluster.Max.X - farCluster.Min.X, (nearCluster.Max.X - nearCluster.Min.X) * 10.0f);
    EXPECT_GT(farCluster.Max.Y - farCluster.Min.Y, (nearCluster.Max.Y - nearCluster.Min.Y) * 10.0f);
}

TEST(ClusteredLightGridTest, AnOrthographicProjectionProducesABoxGrid)
{
    // Not a supported-in-passing accident: the shape comes from unprojecting two NDC depths and
    // interpolating between them, which is linear in view space for both projections. Under an
    // orthographic camera every slice is the same size, and that is the check.
    ClusteredLightGrid grid(4, 4, 6);
    grid.setProjection(Matrix::CreateOrthographic(20.0f, 10.0f, kNear, kFar), kNear, kFar);

    const BoundingBox nearCluster = grid.clusterBounds(1, 1, 0);
    const BoundingBox farCluster  = grid.clusterBounds(1, 1, 5);
    EXPECT_NEAR(nearCluster.Max.X - nearCluster.Min.X, 5.0f, 1e-3f);
    EXPECT_NEAR(nearCluster.Max.Y - nearCluster.Min.Y, 2.5f, 1e-3f);
    EXPECT_NEAR(farCluster.Max.X - farCluster.Min.X, 5.0f, 1e-3f);
    EXPECT_NEAR(farCluster.Max.Y - farCluster.Min.Y, 2.5f, 1e-3f);
}

TEST(ClusteredLightGridTest, ABoundsCoordinateOutsideTheGridIsRejected)
{
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);
    EXPECT_THROW(grid.clusterBounds(16, 0, 0), std::out_of_range);
    EXPECT_THROW(grid.clusterBounds(0, 8, 0), std::out_of_range);
    EXPECT_THROW(grid.clusterBounds(0, 0, 24), std::out_of_range);
    EXPECT_THROW(grid.clusterBounds(-1, 0, 0), std::out_of_range);
}

} // namespace

#endif // CNA_CNAEXT
