// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1406..MOD-1411: level-of-detail selection and frustum culling.
//
// Both classes answer a question rather than draw anything, so they are testable exactly: the
// selection at a boundary, the selection either side of it, what hysteresis holds and what it
// deliberately does not hold, and which of a set of bounds a known frustum keeps.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/LodGroupEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::LodGroupEXT;
using CNA::Graphics::LodSelectionMode;

namespace {

    /// A group with three levels, added out of order so the sort is exercised too.
    LodGroupEXT ThreeLevels()
    {
        LodGroupEXT group;
        group.addLevel(50.0f, nullptr);
        group.addLevel(10.0f, nullptr);
        group.addLevel(200.0f, nullptr);
        return group;
    }

    /// A camera at the origin looking down -Z, with a 90 degree vertical field of view.
    Matrix LookingDownNegativeZ()
    {
        const Matrix view = Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f),
                                                 Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
        return view * projection;
    }

} // namespace

// ---------------------------------------------------------------------------
// LodGroupEXT (MOD-1406)

TEST(LodGroupEXTTest, LevelsAreSortedOnInsertAndSelectedByDistance)
{
    LodGroupEXT group = ThreeLevels();
    ASSERT_EQ(group.getLevels().size(), 3u);
    EXPECT_FLOAT_EQ(group.getLevels()[0].MaxDistance, 10.0f);
    EXPECT_FLOAT_EQ(group.getLevels()[1].MaxDistance, 50.0f);
    EXPECT_FLOAT_EQ(group.getLevels()[2].MaxDistance, 200.0f);

    EXPECT_EQ(group.selectIndex(0.0f), 0);
    EXPECT_EQ(group.selectIndex(9.9f), 0);
    // Exactly at a boundary the level that ends there has ended: 10 is no longer level 0.
    EXPECT_EQ(group.selectIndex(10.0f), 1);
    EXPECT_EQ(group.selectIndex(49.9f), 1);
    EXPECT_EQ(group.selectIndex(50.0f), 2);
    EXPECT_EQ(group.selectIndex(199.9f), 2);
    // Past the last level nothing is drawn at all, which is what makes the last distance useful.
    EXPECT_EQ(group.selectIndex(200.0f), -1);
    EXPECT_EQ(group.selectIndex(1e9f), -1);
}

TEST(LodGroupEXTTest, ANegativeDistanceSelectsTheFinestLevelAndAnEmptyGroupSelectsNothing)
{
    LodGroupEXT group = ThreeLevels();
    EXPECT_EQ(group.selectIndex(-5.0f), 0);

    LodGroupEXT empty;
    EXPECT_EQ(empty.selectIndex(1.0f), -1);
    EXPECT_EQ(empty.select(1.0f), nullptr);

    group.clear();
    EXPECT_TRUE(group.getLevels().empty());
    EXPECT_EQ(group.selectIndex(1.0f), -1);
}

TEST(LodGroupEXTTest, ANonPositiveLevelDistanceIsRefused)
{
    LodGroupEXT group;
    EXPECT_THROW(group.addLevel(0.0f, nullptr), std::invalid_argument);
    EXPECT_THROW(group.addLevel(-1.0f, nullptr), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Hysteresis (MOD-1407)

TEST(LodGroupEXTTest, WithoutHysteresisAnOscillatingDistanceFlipsEveryFrame)
{
    // The behaviour hysteresis exists to fix, asserted first so the fix has something to be
    // measured against rather than only being described.
    LodGroupEXT group = ThreeLevels();
    int changes = 0;
    int previous = group.selectIndex(9.5f);
    for (int frame = 0; frame < 10; ++frame)
    {
        const int index = group.selectIndex(frame % 2 == 0 ? 10.5f : 9.5f);
        if (index != previous) ++changes;
        previous = index;
    }
    EXPECT_EQ(changes, 10) << "the boundary case this test is built on stopped oscillating";
}

TEST(LodGroupEXTTest, HysteresisHoldsTheLevelWithinTheMarginAndReleasesItOutside)
{
    LodGroupEXT group = ThreeLevels();
    group.setHysteresis(2.0f);
    EXPECT_FLOAT_EQ(group.getHysteresis(), 2.0f);

    EXPECT_EQ(group.selectIndex(9.0f), 0);
    // Past the boundary but inside the margin: still level 0.
    EXPECT_EQ(group.selectIndex(10.5f), 0);
    EXPECT_EQ(group.selectIndex(11.9f), 0);
    // Outside it: the change is real.
    EXPECT_EQ(group.selectIndex(12.5f), 1);
    // And coming back is sticky in the same way.
    EXPECT_EQ(group.selectIndex(9.5f), 1);
    EXPECT_EQ(group.selectIndex(7.5f), 0);

    int changes = 0;
    int previous = group.selectIndex(9.5f);
    for (int frame = 0; frame < 10; ++frame)
    {
        const int index = group.selectIndex(frame % 2 == 0 ? 10.5f : 9.5f);
        if (index != previous) ++changes;
        previous = index;
    }
    EXPECT_EQ(changes, 0) << "hysteresis did not stop the flapping it exists for";
}

TEST(LodGroupEXTTest, HysteresisDoesNotHoldASkippedLevelOrSurviveAReset)
{
    // A teleport past two boundaries is a real change, not a wobble; holding it would be worse
    // than the flicker hysteresis prevents.
    LodGroupEXT group = ThreeLevels();
    group.setHysteresis(5.0f);
    EXPECT_EQ(group.selectIndex(5.0f), 0);
    EXPECT_EQ(group.selectIndex(120.0f), 2);

    EXPECT_EQ(group.selectIndex(52.0f), 2);
    group.resetHysteresis();
    EXPECT_EQ(group.selectIndex(52.0f), 2);
    group.resetHysteresis();
    EXPECT_EQ(group.selectIndex(49.0f), 1);

    group.setHysteresis(-1.0f);
    EXPECT_FLOAT_EQ(group.getHysteresis(), 0.0f);
}

// ---------------------------------------------------------------------------
// Screen-space error (MOD-1408)

TEST(LodGroupEXTTest, TheProjectedRadiusMatchesTheDocumentedFormula)
{
    LodGroupEXT group;
    group.setScreenSpaceParameters(1.0f, MathHelper::PiOver2, 720.0f);
    // fov 90 degrees: half-extent at distance d is exactly d, so the projected radius is
    // radius * height / (2 * d).
    EXPECT_NEAR(group.projectedRadiusPixels(1.0f), 360.0f, 1e-3f);
    EXPECT_NEAR(group.projectedRadiusPixels(10.0f), 36.0f, 1e-3f);
    EXPECT_NEAR(group.projectedRadiusPixels(360.0f), 1.0f, 1e-3f);
    EXPECT_GT(group.projectedRadiusPixels(0.0f), 1e30f);

    group.setScreenSpaceParameters(2.0f, MathHelper::PiOver2, 720.0f);
    EXPECT_NEAR(group.projectedRadiusPixels(10.0f), 72.0f, 1e-3f);
}

TEST(LodGroupEXTTest, ScreenSpaceSelectionPicksTheLevelByProjectedSize)
{
    LodGroupEXT group;
    // Read as minimum pixel sizes: 100 px or more is the finest level, 20 px the middle one,
    // 5 px the coarsest, and below that nothing is drawn.
    group.addLevel(100.0f, nullptr);
    group.addLevel(20.0f, nullptr);
    group.addLevel(5.0f, nullptr);
    group.setSelectionMode(LodSelectionMode::ScreenSpaceError);
    EXPECT_EQ(group.getSelectionMode(), LodSelectionMode::ScreenSpaceError);
    group.setScreenSpaceParameters(1.0f, MathHelper::PiOver2, 720.0f);

    EXPECT_EQ(group.selectIndex(1.0f), 0);      // 360 px
    EXPECT_EQ(group.selectIndex(3.6f), 0);      // 100 px, exactly at the threshold
    EXPECT_EQ(group.selectIndex(4.0f), 1);      // 90 px
    EXPECT_EQ(group.selectIndex(18.0f), 1);     // 20 px
    EXPECT_EQ(group.selectIndex(20.0f), 2);     // 18 px
    EXPECT_EQ(group.selectIndex(72.0f), 2);     // 5 px
    EXPECT_EQ(group.selectIndex(100.0f), -1);   // 3.6 px: too small to draw at all
}

TEST(LodGroupEXTTest, ScreenSpaceParametersAreValidated)
{
    LodGroupEXT group;
    EXPECT_THROW(group.setScreenSpaceParameters(0.0f, 1.0f, 720.0f), std::invalid_argument);
    EXPECT_THROW(group.setScreenSpaceParameters(1.0f, 0.0f, 720.0f), std::invalid_argument);
    EXPECT_THROW(group.setScreenSpaceParameters(1.0f, 4.0f, 720.0f), std::invalid_argument);
    EXPECT_THROW(group.setScreenSpaceParameters(1.0f, 1.0f, 0.0f), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// FrustumCullerEXT (MOD-1409/MOD-1410)

TEST(FrustumCullerEXTTest, VisibilityMatchesHandComputedCases)
{
    FrustumCullerEXT culler;
    culler.setViewProjection(LookingDownNegativeZ());

    // Straight ahead, between the near and far planes.
    EXPECT_TRUE(culler.isVisible(BoundingBox(Vector3(-1.0f, -1.0f, -11.0f),
                                             Vector3(1.0f, 1.0f, -9.0f))));
    // Behind the camera.
    EXPECT_FALSE(culler.isVisible(BoundingBox(Vector3(-1.0f, -1.0f, 9.0f),
                                              Vector3(1.0f, 1.0f, 11.0f))));
    // Past the far plane.
    EXPECT_FALSE(culler.isVisible(BoundingBox(Vector3(-1.0f, -1.0f, -201.0f),
                                              Vector3(1.0f, 1.0f, -199.0f))));
    // Far off to the side: at 90 degrees the frustum's half-width at z = -10 is 10.
    EXPECT_FALSE(culler.isVisible(BoundingBox(Vector3(50.0f, -1.0f, -11.0f),
                                              Vector3(52.0f, 1.0f, -9.0f))));
    // Straddling the edge is still visible -- partial containment counts.
    EXPECT_TRUE(culler.isVisible(BoundingBox(Vector3(9.0f, -1.0f, -11.0f),
                                             Vector3(11.0f, 1.0f, -9.0f))));
    EXPECT_TRUE(culler.isVisible(BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 1.0f)));
    EXPECT_FALSE(culler.isVisible(BoundingSphere(Vector3(0.0f, 0.0f, 10.0f), 1.0f)));
}

TEST(FrustumCullerEXTTest, CullingReportsTheVisibleIndicesAndReusesItsBuffer)
{
    FrustumCullerEXT culler;
    culler.setCamera(Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f),
                                          Vector3(0.0f, 1.0f, 0.0f)),
                     Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 1.0f, 100.0f));

    const std::vector<BoundingBox> boxes{
        BoundingBox(Vector3(-1.0f, -1.0f, -11.0f), Vector3(1.0f, 1.0f, -9.0f)),   // visible
        BoundingBox(Vector3(-1.0f, -1.0f, 9.0f), Vector3(1.0f, 1.0f, 11.0f)),     // behind
        BoundingBox(Vector3(-1.0f, -1.0f, -31.0f), Vector3(1.0f, 1.0f, -29.0f)),  // visible
        BoundingBox(Vector3(500.0f, -1.0f, -11.0f), Vector3(502.0f, 1.0f, -9.0f)) // beside
    };

    std::vector<std::size_t> visible;
    EXPECT_EQ(culler.cull(boxes, visible), 2u);
    ASSERT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0], 0u);
    EXPECT_EQ(visible[1], 2u);

    // The buffer is reused rather than reallocated: culling every frame must stop allocating.
    const std::size_t capacity = visible.capacity();
    EXPECT_EQ(culler.cull(boxes, visible), 2u);
    EXPECT_EQ(visible.capacity(), capacity);

    const std::vector<BoundingSphere> spheres{
        BoundingSphere(Vector3(0.0f, 0.0f, -10.0f), 1.0f),
        BoundingSphere(Vector3(0.0f, 0.0f, 10.0f), 1.0f),
    };
    EXPECT_EQ(culler.cull(spheres, visible), 1u);
    EXPECT_EQ(visible[0], 0u);

    const std::vector<BoundingBox> none;
    EXPECT_EQ(culler.cull(none, visible), 0u);
    EXPECT_TRUE(visible.empty());
}

TEST(FrustumCullerEXTTest, TransformCullingKeepsInputOrderAndKeepsUnboundedEntries)
{
    FrustumCullerEXT culler;
    culler.setViewProjection(LookingDownNegativeZ());

    const std::vector<Matrix> transforms{
        Matrix::CreateTranslation(Vector3(0.0f, 0.0f, -10.0f)),
        Matrix::CreateTranslation(Vector3(0.0f, 0.0f, 10.0f)),
        Matrix::CreateTranslation(Vector3(0.0f, 0.0f, -30.0f)),
        Matrix::CreateTranslation(Vector3(7.0f, 7.0f, -7.0f)),
    };
    const std::vector<BoundingBox> bounds{
        BoundingBox(Vector3(-1.0f, -1.0f, -11.0f), Vector3(1.0f, 1.0f, -9.0f)),
        BoundingBox(Vector3(-1.0f, -1.0f, 9.0f), Vector3(1.0f, 1.0f, 11.0f)),
        BoundingBox(Vector3(-1.0f, -1.0f, -31.0f), Vector3(1.0f, 1.0f, -29.0f)),
        // the fourth transform deliberately has no bounds
    };

    std::vector<Matrix> visible;
    EXPECT_EQ(culler.cullTransforms(transforms, bounds, visible), 3u);
    ASSERT_EQ(visible.size(), 3u);
    EXPECT_FLOAT_EQ(visible[0].M43, -10.0f);
    EXPECT_FLOAT_EQ(visible[1].M43, -30.0f);
    EXPECT_FLOAT_EQ(visible[2].M43, -7.0f) << "an entry with no bounds was dropped rather than kept";
}

#endif // CNA_CNAEXT
