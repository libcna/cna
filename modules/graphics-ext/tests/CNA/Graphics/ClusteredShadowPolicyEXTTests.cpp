// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2047: which of many lights gets a shadow map.
//
// The section's honest statement is that clustered shading lifted the limit on lighting and none
// at all on shadowing, so this is a budget rather than a solution. What the tests check is that the
// budget is spent on a stated rule and spends it the same way twice: brightest at the camera wins,
// invisible lights lose, and an incumbent is not evicted by a rival that merely edged ahead.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/ClusteredShadowPolicyEXT.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::ClusteredLightEXT;
using CNA::Graphics::ClusteredLightSetEXT;
using CNA::Graphics::ClusteredLightType;
using CNA::Graphics::ClusteredShadowPolicyEXT;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

Matrix View()
{
    return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up);
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(1.2f, 1.0f, 0.5f, 200.0f);
}

ClusteredLightEXT MakeLight(const Vector3& position, const float range, const float intensity,
                            const bool castsShadows)
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Point;
    light.Position = position;
    light.Range = range;
    light.Intensity = intensity;
    light.CastsShadows = castsShadows;
    return light;
}

TEST(ClusteredShadowPolicyTest, ANegativeBudgetIsRefusedAndZeroIsAllowed)
{
    EXPECT_THROW(ClusteredShadowPolicyEXT(-1), std::invalid_argument);
    EXPECT_NO_THROW(ClusteredShadowPolicyEXT(0));

    ClusteredShadowPolicyEXT policy;
    EXPECT_EQ(policy.getBudget(), ClusteredShadowPolicyEXT::kDefaultBudget);
    policy.setBudget(2);
    EXPECT_EQ(policy.getBudget(), 2);
    policy.setBudget(-5);
    EXPECT_EQ(policy.getBudget(), 2) << "a negative budget must be ignored, not applied";

    policy.setHysteresis(2.0f);
    EXPECT_FLOAT_EQ(policy.getHysteresis(), 2.0f);
    policy.setHysteresis(0.5f);
    EXPECT_FLOAT_EQ(policy.getHysteresis(), 2.0f)
        << "hysteresis below 1 would make the selection change for no reason";
}

TEST(ClusteredShadowPolicyTest, OnlyLightsThatAskedAreConsidered)
{
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f), 30.0f, 10.0f, false));
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -11.0f), 30.0f, 1.0f, true));

    ClusteredShadowPolicyEXT policy(4);
    policy.select(lights, View(), Projection(), Vector3::Zero);

    EXPECT_EQ(policy.getRequestCount(), 1);
    EXPECT_EQ(policy.getSelected().size(), 1u);
    EXPECT_TRUE(policy.isSelected(1));
    EXPECT_FALSE(policy.isSelected(0))
        << "a light ten times brighter took a map it never asked for";
    EXPECT_FLOAT_EQ(policy.getScore(0), 0.0f);
    EXPECT_EQ(policy.getRefusedCount(), 0);
}

TEST(ClusteredShadowPolicyTest, TheBudgetGoesToTheBrightestAtTheCamera)
{
    // Four lights the same distance away, differing only in intensity, and a budget of two.
    ClusteredLightSetEXT lights;
    for (int i = 0; i < 4; ++i)
        lights.add(MakeLight(Vector3(static_cast<float>(i) - 1.5f, 0.0f, -10.0f), 40.0f,
                             1.0f + static_cast<float>(i), true));

    ClusteredShadowPolicyEXT policy(2);
    policy.select(lights, View(), Projection(), Vector3::Zero);

    ASSERT_EQ(policy.getSelected().size(), 2u);
    EXPECT_EQ(policy.getSelected()[0], 3) << "the brightest light did not come first";
    EXPECT_EQ(policy.getSelected()[1], 2);
    EXPECT_EQ(policy.getRequestCount(), 4);
    EXPECT_EQ(policy.getRefusedCount(), 2) << "the number worth logging is wrong";
}

TEST(ClusteredShadowPolicyTest, DistanceBeatsRawIntensity)
{
    // The ranking is contribution at the camera, not brightness in the abstract: a dim lamp in the
    // room wins over a bright one across the map, which is what a viewer would expect the shadow
    // budget to do.
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -3.0f), 20.0f, 1.0f, true));      // near, dim
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -80.0f), 200.0f, 8.0f, true));    // far, bright

    ClusteredShadowPolicyEXT policy(1);
    policy.select(lights, View(), Projection(), Vector3::Zero);

    ASSERT_EQ(policy.getSelected().size(), 1u);
    EXPECT_EQ(policy.getSelected()[0], 0) << "the distant light took the only map";
    EXPECT_GT(policy.getScore(0), policy.getScore(1));
}

TEST(ClusteredShadowPolicyTest, ALightOutsideTheFrustumScoresNothing)
{
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, 50.0f), 10.0f, 100.0f, true));   // behind the camera
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f), 30.0f, 1.0f, true));    // in view

    ClusteredShadowPolicyEXT policy(4);
    policy.select(lights, View(), Projection(), Vector3::Zero);

    EXPECT_FLOAT_EQ(policy.getScore(0), 0.0f) << "a light nobody can see was scored";
    EXPECT_GT(policy.getScore(1), 0.0f);
    EXPECT_FALSE(policy.isSelected(0));
    EXPECT_TRUE(policy.isSelected(1));
    EXPECT_EQ(policy.getRefusedCount(), 1) << "the invisible light still asked and still lost";
}

TEST(ClusteredShadowPolicyTest, AnIncumbentIsNotEvictedByARivalThatMerelyEdgedAhead)
{
    // Without this the shadow blinks between two lights whose scores cross every few frames, which
    // reads as a bug in the shadow system rather than as a budget doing its job.
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f), 40.0f, 1.00f, true));
    lights.add(MakeLight(Vector3(1.0f, 0.0f, -10.0f), 40.0f, 0.99f, true));

    ClusteredShadowPolicyEXT policy(1);
    policy.setHysteresis(1.25f);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    ASSERT_EQ(policy.getSelected().size(), 1u);
    ASSERT_EQ(policy.getSelected()[0], 0);

    // Light 1 edges ahead by a few per cent. The incumbent keeps the map.
    ClusteredLightEXT stronger = lights.getAt(1);
    stronger.Intensity = 1.10f;
    lights.replaceAt(1, stronger);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    EXPECT_EQ(policy.getSelected()[0], 0) << "a three per cent lead evicted the incumbent";

    // Twice as bright is not an edge, and it takes the map.
    stronger.Intensity = 2.0f;
    lights.replaceAt(1, stronger);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    EXPECT_EQ(policy.getSelected()[0], 1) << "a light twice as bright never took the map";
}

TEST(ClusteredShadowPolicyTest, ResetForgetsTheIncumbents)
{
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f), 40.0f, 1.00f, true));
    lights.add(MakeLight(Vector3(1.0f, 0.0f, -10.0f), 40.0f, 0.90f, true));

    ClusteredShadowPolicyEXT policy(1);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    ASSERT_EQ(policy.getSelected()[0], 0);

    // Light 1 pulls ahead, but not by the margin -- so the incumbent holds it.
    ClusteredLightEXT brighter = lights.getAt(1);
    brighter.Intensity = 1.10f;
    lights.replaceAt(1, brighter);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    ASSERT_EQ(policy.getSelected()[0], 0);

    policy.reset();
    EXPECT_TRUE(policy.getSelected().empty());
    EXPECT_EQ(policy.getRequestCount(), 0);

    policy.select(lights, View(), Projection(), Vector3::Zero);
    EXPECT_EQ(policy.getSelected()[0], 1) << "with no incumbent the brighter light must win";
}

TEST(ClusteredShadowPolicyTest, AZeroBudgetHandsOutNothingAndSaysSo)
{
    ClusteredLightSetEXT lights;
    for (int i = 0; i < 5; ++i)
        lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f - static_cast<float>(i)), 40.0f, 1.0f,
                             true));

    ClusteredShadowPolicyEXT policy(0);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    EXPECT_TRUE(policy.getSelected().empty());
    EXPECT_EQ(policy.getRequestCount(), 5);
    EXPECT_EQ(policy.getRefusedCount(), 5);
}

TEST(ClusteredShadowPolicyTest, TheSelectionIsStableAcrossIdenticalFrames)
{
    // Two lights with exactly equal scores must not swap places from one frame to the next; the
    // sort is stable for that reason and the test says so rather than leaving it to the algorithm.
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(-1.0f, 0.0f, -10.0f), 40.0f, 1.0f, true));
    lights.add(MakeLight(Vector3(1.0f, 0.0f, -10.0f), 40.0f, 1.0f, true));

    ClusteredShadowPolicyEXT policy(1);
    policy.select(lights, View(), Projection(), Vector3::Zero);
    const std::vector<int> first = policy.getSelected();
    for (int frame = 0; frame < 5; ++frame)
    {
        policy.select(lights, View(), Projection(), Vector3::Zero);
        EXPECT_EQ(policy.getSelected(), first) << "the selection moved on frame " << frame;
    }
}

TEST(ClusteredShadowPolicyTest, AScoreForALightThatWasNotThereIsRefused)
{
    ClusteredLightSetEXT lights;
    lights.add(MakeLight(Vector3(0.0f, 0.0f, -10.0f), 40.0f, 1.0f, true));

    ClusteredShadowPolicyEXT policy;
    policy.select(lights, View(), Projection(), Vector3::Zero);
    EXPECT_THROW((void)policy.getScore(1), std::out_of_range);
    EXPECT_THROW((void)policy.getScore(-1), std::out_of_range);
}

} // namespace

#endif // CNA_CNAEXT
