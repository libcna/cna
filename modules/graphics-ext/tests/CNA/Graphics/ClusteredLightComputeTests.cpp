// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2043: the same sort on the GPU, and the only claim worth making about it --
// that it produces the *same list*, not a similar one.
//
// Everything downstream refers to lights by index, so "close enough" has no meaning here: an
// assignment that differs by one entry lights a different object. The tests therefore compare the
// two paths element for element, on scenes chosen to sit on the boundaries where they could
// plausibly disagree.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightCompute.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::ClusteredLightAssignment;
using CNA::Graphics::ClusteredLightCompute;
using CNA::Graphics::ClusteredLightGrid;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr float kNear = 0.5f;
constexpr float kFar  = 120.0f;

ClusteredLightGrid MakeGrid()
{
    ClusteredLightGrid grid;
    grid.setProjection(Matrix::CreatePerspectiveFieldOfView(1.0471975512f, 16.0f / 9.0f, kNear,
                                                            kFar),
                       kNear, kFar);
    return grid;
}

#define CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute)                                                  \
    do {                                                                                           \
        if (!(compute).isSupported())                                                              \
            GTEST_SKIP() << "no compute path here: " << (compute).getUnsupportedReason();          \
    } while (false)

void ExpectIdenticalAssignments(const ClusteredLightGrid& grid, const Matrix& view,
                                const std::vector<BoundingSphere>& lights,
                                ClusteredLightCompute& compute)
{
    ClusteredLightAssignment onCpu;
    onCpu.assign(grid, view, lights);

    ClusteredLightAssignment onGpu;
    compute.assign(grid, view, lights, onGpu);
    ASSERT_TRUE(compute.usedCompute()) << "the GPU path silently fell back";
    ASSERT_FALSE(compute.hasOverflowed())
        << "a cluster filled past the stride, so the two paths are not comparable here";

    ASSERT_EQ(onGpu.getClusterCount(), onCpu.getClusterCount());
    ASSERT_EQ(onGpu.getLightCount(), onCpu.getLightCount());
    EXPECT_EQ(onGpu.getTotalReferenceCount(), onCpu.getTotalReferenceCount());

    int mismatches = 0;
    for (int cluster = 0; cluster < onCpu.getClusterCount(); ++cluster)
    {
        const std::span<const int> cpu = onCpu.lightsInCluster(cluster);
        const std::span<const int> gpu = onGpu.lightsInCluster(cluster);
        if (cpu.size() != gpu.size() || !std::equal(cpu.begin(), cpu.end(), gpu.begin()))
        {
            if (++mismatches <= 5)
                ADD_FAILURE() << "cluster " << cluster << ": CPU has " << cpu.size()
                              << " lights, GPU has " << gpu.size();
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST(ClusteredLightComputeTest, ADeviceWithoutComputeStillProducesAnAssignment)
{
    // The fallback is the point of this one: whatever the device, assign() has to leave a usable
    // result behind, because a frame with no lights sorted is worse than a frame sorted slowly.
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    const ClusteredLightGrid grid = MakeGrid();

    const std::vector<BoundingSphere> lights = {
        BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 6.0f),
    };

    ClusteredLightAssignment result;
    compute.assign(grid, Matrix::getIdentityProperty(), lights, result);

    EXPECT_EQ(result.getClusterCount(), grid.getClusterCount());
    EXPECT_EQ(result.getLightCount(), 1);
    EXPECT_GT(result.getTotalReferenceCount(), 0);
    if (!compute.isSupported())
        EXPECT_FALSE(compute.usedCompute()) << "a device without compute claimed to have used it";
    EXPECT_FALSE(compute.getUnsupportedReason().empty() && !compute.isSupported())
        << "an unsupported path must say why";
}

TEST(ClusteredLightComputeTest, ANonPositiveStrideIsRefused)
{
    GraphicsDevice gd;
    EXPECT_THROW(ClusteredLightCompute(gd, 0), std::invalid_argument);
    EXPECT_THROW(ClusteredLightCompute(gd, -4), std::invalid_argument);
}

TEST(ClusteredLightComputeTest, OneLightSortsIdenticallyOnBothPaths)
{
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    ExpectIdenticalAssignments(MakeGrid(), Matrix::getIdentityProperty(),
                               {BoundingSphere(Vector3(2.0f, 1.0f, -15.0f), 6.0f)}, compute);
}

TEST(ClusteredLightComputeTest, ManyScatteredLightsSortIdenticallyOnBothPaths)
{
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    const Matrix view = Matrix::CreateLookAt(Vector3(1.0f, 2.0f, 30.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));
    std::vector<BoundingSphere> lights;
    for (int i = 0; i < 40; ++i)
    {
        const float t = static_cast<float>(i);
        lights.emplace_back(Vector3(std::sin(t * 0.7f) * 18.0f, std::cos(t * 1.1f) * 9.0f,
                                    std::sin(t * 0.31f) * 40.0f),
                            1.0f + std::fabs(std::sin(t * 0.53f)) * 9.0f);
    }
    ExpectIdenticalAssignments(MakeGrid(), view, lights, compute);
}

TEST(ClusteredLightComputeTest, LightsOutsideTheFrustumSortIdenticallyOnBothPaths)
{
    // The CPU path narrows by depth before it starts and the GPU path does not narrow at all, so
    // the four ways a light can be outside the frustum are exactly where the two could part company.
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    const std::vector<BoundingSphere> lights = {
        BoundingSphere(Vector3(0.0f, 0.0f, 20.0f), 3.0f),        // behind the camera
        BoundingSphere(Vector3(0.0f, 0.0f, -400.0f), 10.0f),     // past the far plane
        BoundingSphere(Vector3(0.0f, 0.0f, -0.2f), 2.0f),        // straddling the near plane
        BoundingSphere(Vector3(500.0f, 0.0f, -20.0f), 4.0f),     // off to the side
        BoundingSphere(Vector3(0.0f, 0.0f, -14.0f), 5.0f),       // and one ordinary light
    };
    ExpectIdenticalAssignments(MakeGrid(), Matrix::getIdentityProperty(), lights, compute);
}

TEST(ClusteredLightComputeTest, AZeroRadiusLightIsSkippedOnBothPaths)
{
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    ExpectIdenticalAssignments(MakeGrid(), Matrix::getIdentityProperty(),
                               {BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 0.0f),
                                BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 6.0f)},
                               compute);
}

TEST(ClusteredLightComputeTest, NoLightsAtAllSortsIdenticallyOnBothPaths)
{
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    ExpectIdenticalAssignments(MakeGrid(), Matrix::getIdentityProperty(), {}, compute);
}

TEST(ClusteredLightComputeTest, AClusterPastTheStrideIsReportedRatherThanTruncatedInSilence)
{
    // A GPU cannot grow an array, so the per-cluster capacity is fixed and overflow is possible.
    // What must not happen is that it passes unnoticed: the count is a number a game can act on.
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd, 4);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    // Ten lights piled on top of each other, so every cluster they touch holds all ten.
    std::vector<BoundingSphere> lights;
    for (int i = 0; i < 10; ++i)
        lights.emplace_back(Vector3(0.0f, 0.0f, -12.0f), 5.0f);

    ClusteredLightAssignment result;
    compute.assign(MakeGrid(), Matrix::getIdentityProperty(), lights, result);
    ASSERT_TRUE(compute.usedCompute());
    EXPECT_TRUE(compute.hasOverflowed()) << "the overflow was not reported";
    EXPECT_LE(result.getMaxLightsPerCluster(), 4) << "the stride was not honoured";
    EXPECT_GT(result.getTotalReferenceCount(), 0) << "overflow lost the whole assignment";
}

TEST(ClusteredLightComputeTest, TooManyLightsIsRefusedOnTheGpuPathToo)
{
    GraphicsDevice gd;
    ClusteredLightCompute compute(gd);
    CNA_SKIP_WITHOUT_CLUSTER_COMPUTE(compute);

    ClusteredLightAssignment result;
    const std::vector<BoundingSphere> tooMany(ClusteredLightAssignment::kMaxLights + 1,
                                              BoundingSphere(Vector3::Zero, 1.0f));
    EXPECT_THROW(compute.assign(MakeGrid(), Matrix::getIdentityProperty(), tooMany, result),
                 std::invalid_argument);
}

TEST(ClusteredLightComputeTest, AnAdoptedAssignmentIsValidatedRatherThanTrusted)
{
    // adopt() exists for the GPU path, which means it is the one door into an assignment that did
    // not build itself. An inconsistent pair of arrays lights the wrong objects rather than
    // failing, so it is checked at the door.
    ClusteredLightAssignment assignment;
    EXPECT_THROW(assignment.adopt(2, {}, {}), std::invalid_argument);
    EXPECT_THROW(assignment.adopt(2, {1, 2}, {0}), std::invalid_argument) << "offsets not from zero";
    EXPECT_THROW(assignment.adopt(2, {0, 2}, {0}), std::invalid_argument) << "last offset is wrong";
    EXPECT_THROW(assignment.adopt(2, {0, 2, 1}, {0, 1}), std::invalid_argument) << "not monotone";
    EXPECT_THROW(assignment.adopt(1, {0, 1}, {5}), std::invalid_argument) << "index has no light";

    EXPECT_NO_THROW(assignment.adopt(2, {0, 1, 2}, {1, 0}));
    EXPECT_EQ(assignment.getClusterCount(), 2);
    EXPECT_EQ(assignment.getMaxLightsPerCluster(), 1);
    ASSERT_EQ(assignment.lightsInCluster(0).size(), 1u);
    EXPECT_EQ(assignment.lightsInCluster(0)[0], 1);
}

} // namespace

#endif // CNA_CNAEXT
