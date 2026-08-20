// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2042: lights sorted into clusters on the CPU, checked against brute force.
//
// The implementation narrows the search by depth before it starts -- a sphere spans a known range
// of view distances, so most of the twenty-four slices are never visited. That narrowing is exactly
// the kind of optimisation that silently drops a light at a boundary, so the reference here visits
// every cluster in the grid with no narrowing at all and the two results must be identical.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::ClusteredLightAssignment;
using CNA::Graphics::ClusteredLightGrid;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

constexpr float kFieldOfView = 1.0471975512f;
constexpr float kAspect      = 16.0f / 9.0f;
constexpr float kNear        = 0.5f;
constexpr float kFar         = 120.0f;

Matrix TestProjection()
{
    return Matrix::CreatePerspectiveFieldOfView(kFieldOfView, kAspect, kNear, kFar);
}

ClusteredLightGrid MakeGrid()
{
    ClusteredLightGrid grid;
    grid.setProjection(TestProjection(), kNear, kFar);
    return grid;
}

/// Every cluster, every light, no narrowing of any kind.
std::vector<std::vector<int>> BruteForce(const ClusteredLightGrid& grid, const Matrix& view,
                                         const std::vector<BoundingSphere>& lights)
{
    std::vector<std::vector<int>> result(static_cast<std::size_t>(grid.getClusterCount()));
    for (int slice = 0; slice < grid.getSliceCount(); ++slice)
        for (int y = 0; y < grid.getTilesY(); ++y)
            for (int x = 0; x < grid.getTilesX(); ++x)
            {
                const BoundingBox box = grid.clusterBounds(x, y, slice);
                for (int light = 0; light < static_cast<int>(lights.size()); ++light)
                {
                    const BoundingSphere& sphere = lights[static_cast<std::size_t>(light)];
                    if (!(sphere.Radius > 0.0f)) continue;
                    const Vector3 centre = Vector3::Transform(sphere.Center, view);

                    const Vector3 nearest(std::clamp(centre.X, box.Min.X, box.Max.X),
                                          std::clamp(centre.Y, box.Min.Y, box.Max.Y),
                                          std::clamp(centre.Z, box.Min.Z, box.Max.Z));
                    const float dx = centre.X - nearest.X;
                    const float dy = centre.Y - nearest.Y;
                    const float dz = centre.Z - nearest.Z;
                    if (dx * dx + dy * dy + dz * dz <= sphere.Radius * sphere.Radius)
                        result[static_cast<std::size_t>(grid.clusterIndex(x, y, slice))]
                            .push_back(light);
                }
            }
    return result;
}

void ExpectMatchesBruteForce(const ClusteredLightGrid& grid, const Matrix& view,
                             const std::vector<BoundingSphere>& lights)
{
    ClusteredLightAssignment assignment;
    assignment.assign(grid, view, lights);

    const std::vector<std::vector<int>> expected = BruteForce(grid, view, lights);
    ASSERT_EQ(assignment.getClusterCount(), static_cast<int>(expected.size()));

    int mismatches = 0;
    for (int cluster = 0; cluster < assignment.getClusterCount(); ++cluster)
    {
        const std::span<const int> actual = assignment.lightsInCluster(cluster);
        const std::vector<int>& reference = expected[static_cast<std::size_t>(cluster)];
        if (static_cast<int>(actual.size()) != static_cast<int>(reference.size()) ||
            !std::equal(actual.begin(), actual.end(), reference.begin()))
        {
            if (++mismatches <= 5)
                ADD_FAILURE() << "cluster " << cluster << " has " << actual.size()
                              << " lights, brute force says " << reference.size();
        }
    }
    EXPECT_EQ(mismatches, 0);
}

// ── Agreement with brute force ───────────────────────────────────────────────

TEST(ClusteredLightAssignmentTest, ASingleLightMatchesBruteForce)
{
    const ClusteredLightGrid grid = MakeGrid();
    const Matrix view = Matrix::getIdentityProperty();
    ExpectMatchesBruteForce(grid, view, {BoundingSphere(Vector3(2.0f, 1.0f, -15.0f), 6.0f)});
}

TEST(ClusteredLightAssignmentTest, ManyScatteredLightsMatchBruteForce)
{
    const ClusteredLightGrid grid = MakeGrid();
    const Matrix view = Matrix::CreateLookAt(Vector3(1.0f, 2.0f, 30.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));

    // A deterministic spread rather than random values, so a failure is reproducible.
    std::vector<BoundingSphere> lights;
    for (int i = 0; i < 40; ++i)
    {
        const float t = static_cast<float>(i);
        lights.emplace_back(Vector3(std::sin(t * 0.7f) * 18.0f,
                                    std::cos(t * 1.1f) * 9.0f,
                                    std::sin(t * 0.31f) * 40.0f),
                            1.0f + std::fabs(std::sin(t * 0.53f)) * 9.0f);
    }
    ExpectMatchesBruteForce(grid, view, lights);
}

TEST(ClusteredLightAssignmentTest, LightsOnSliceBoundariesMatchBruteForce)
{
    // The depth narrowing is the one place this can silently lose a light, so lights are placed
    // exactly on slice boundaries -- and just either side of them -- rather than anywhere general.
    const ClusteredLightGrid grid = MakeGrid();
    const Matrix view = Matrix::getIdentityProperty();

    std::vector<BoundingSphere> lights;
    for (int slice = 0; slice <= grid.getSliceCount(); ++slice)
    {
        const float d = grid.sliceDistance(slice);
        lights.emplace_back(Vector3(0.0f, 0.0f, -d), 0.05f);
        lights.emplace_back(Vector3(0.0f, 0.0f, -d * 0.999f), 0.05f);
        lights.emplace_back(Vector3(0.0f, 0.0f, -d * 1.001f), 0.05f);
    }
    ExpectMatchesBruteForce(grid, view, lights);
}

TEST(ClusteredLightAssignmentTest, LightsOutsideTheFrustumMatchBruteForce)
{
    // Behind the camera, past the far plane, straddling the near plane, and off to one side. Each
    // of these takes a different branch of the depth narrowing, and each must still agree.
    const ClusteredLightGrid grid = MakeGrid();
    const Matrix view = Matrix::getIdentityProperty();

    const std::vector<BoundingSphere> lights = {
        BoundingSphere(Vector3(0.0f, 0.0f, 20.0f), 3.0f),        // behind the camera
        BoundingSphere(Vector3(0.0f, 0.0f, -400.0f), 10.0f),     // past the far plane
        BoundingSphere(Vector3(0.0f, 0.0f, -0.2f), 2.0f),        // straddling the near plane
        BoundingSphere(Vector3(500.0f, 0.0f, -20.0f), 4.0f),     // off to the side
        BoundingSphere(Vector3(0.0f, 0.0f, 0.0f), 1.0f),         // on the camera itself
    };
    ExpectMatchesBruteForce(grid, view, lights);
}

TEST(ClusteredLightAssignmentTest, AHugeLightReachesEveryCluster)
{
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(),
                      {BoundingSphere(Vector3(0.0f, 0.0f, -50.0f), 5000.0f)});

    EXPECT_EQ(assignment.getTotalReferenceCount(), grid.getClusterCount());
    EXPECT_EQ(assignment.getMaxLightsPerCluster(), 1);
    for (int cluster = 0; cluster < grid.getClusterCount(); ++cluster)
        ASSERT_EQ(assignment.lightsInCluster(cluster).size(), 1u) << "cluster " << cluster;
}

// ── The storage layout ───────────────────────────────────────────────────────

TEST(ClusteredLightAssignmentTest, TheOffsetsDescribeTheIndexArrayExactly)
{
    // The pair is what gets uploaded, so their consistency is API rather than an internal detail:
    // one more offset than clusters, monotone, ending at the index count.
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(),
                      {BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 8.0f),
                       BoundingSphere(Vector3(6.0f, 0.0f, -30.0f), 12.0f)});

    const std::vector<int>& offsets = assignment.getOffsets();
    ASSERT_EQ(static_cast<int>(offsets.size()), grid.getClusterCount() + 1);
    EXPECT_EQ(offsets.front(), 0);
    EXPECT_EQ(offsets.back(), assignment.getTotalReferenceCount());
    EXPECT_EQ(assignment.getTotalReferenceCount(),
              static_cast<int>(assignment.getIndices().size()));
    for (std::size_t i = 1; i < offsets.size(); ++i)
        ASSERT_GE(offsets[i], offsets[i - 1]) << "the offsets go backwards at " << i;

    int summed = 0;
    for (int cluster = 0; cluster < grid.getClusterCount(); ++cluster)
        summed += static_cast<int>(assignment.lightsInCluster(cluster).size());
    EXPECT_EQ(summed, assignment.getTotalReferenceCount());
    EXPECT_GT(summed, 0) << "two lights inside the frustum reached nothing";
}

TEST(ClusteredLightAssignmentTest, IndicesWithinAClusterAreSortedAndUnique)
{
    // The shader walks the list in order and does not deduplicate, so a repeat would double a
    // light's contribution -- a bug that looks like a brightness tuning problem.
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    std::vector<BoundingSphere> lights;
    for (int i = 0; i < 12; ++i)
        lights.emplace_back(Vector3(static_cast<float>(i) - 6.0f, 0.0f, -12.0f), 9.0f);
    assignment.assign(grid, Matrix::getIdentityProperty(), lights);

    for (int cluster = 0; cluster < grid.getClusterCount(); ++cluster)
    {
        const std::span<const int> list = assignment.lightsInCluster(cluster);
        for (std::size_t i = 1; i < list.size(); ++i)
            ASSERT_LT(list[i - 1], list[i]) << "cluster " << cluster << " is unsorted or repeats";
    }
}

TEST(ClusteredLightAssignmentTest, ReassigningReplacesRatherThanAccumulates)
{
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    const std::vector<BoundingSphere> one = {BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 5.0f)};

    assignment.assign(grid, Matrix::getIdentityProperty(), one);
    const int first = assignment.getTotalReferenceCount();
    assignment.assign(grid, Matrix::getIdentityProperty(), one);
    EXPECT_EQ(assignment.getTotalReferenceCount(), first);
    EXPECT_EQ(assignment.getLightCount(), 1);

    assignment.clear();
    EXPECT_EQ(assignment.getTotalReferenceCount(), 0);
    EXPECT_EQ(assignment.getClusterCount(), 0);
    EXPECT_EQ(assignment.getLightCount(), 0);
    EXPECT_EQ(assignment.getMaxLightsPerCluster(), 0);
}

TEST(ClusteredLightAssignmentTest, AZeroRadiusLightIsCarriedButReachesNothing)
{
    // Its index still has to mean the same thing, so it is skipped rather than removed -- a light
    // switched off by setting its range to zero must not renumber the lights after it.
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(),
                      {BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 0.0f),
                       BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 6.0f)});

    EXPECT_EQ(assignment.getLightCount(), 2);
    for (const int index : assignment.getIndices())
        ASSERT_EQ(index, 1) << "the zero-radius light was assigned to a cluster";
    EXPECT_GT(assignment.getTotalReferenceCount(), 0);
}

// ── Refusals ─────────────────────────────────────────────────────────────────

TEST(ClusteredLightAssignmentTest, AGridWithoutACameraIsRefused)
{
    const ClusteredLightGrid grid;
    ClusteredLightAssignment assignment;
    EXPECT_THROW(assignment.assign(grid, Matrix::getIdentityProperty(),
                                   {BoundingSphere(Vector3::Zero, 1.0f)}),
                 std::runtime_error);
}

TEST(ClusteredLightAssignmentTest, TooManyLightsIsRefused)
{
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    const std::vector<BoundingSphere> tooMany(ClusteredLightAssignment::kMaxLights + 1,
                                              BoundingSphere(Vector3::Zero, 1.0f));
    EXPECT_THROW(assignment.assign(grid, Matrix::getIdentityProperty(), tooMany),
                 std::invalid_argument);
}

TEST(ClusteredLightAssignmentTest, AClusterIndexOutsideTheAssignmentIsRefused)
{
    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(),
                      {BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 5.0f)});
    EXPECT_THROW((void)assignment.lightsInCluster(-1), std::out_of_range);
    EXPECT_THROW((void)assignment.lightsInCluster(grid.getClusterCount()), std::out_of_range);
}

} // namespace

#endif // CNA_CNAEXT
