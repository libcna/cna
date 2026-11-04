// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031SpherePointSeedTest, FirstNanPointPoisonsExtremaButTrailingNanDoesNot)
{
    // XNA CreateFromPoints IL_002a..0040 initializes all SIX extrema from Current.
    // IL_0057..00c7 unordered comparisons never replace a NaN extremum. The final
    // center/radius are NaN; a trailing NaN is ignored after finite extrema exist.
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const auto check=[](const BoundingSphere& sphere) {
        EXPECT_TRUE(std::isnan(sphere.Center.X)); EXPECT_TRUE(std::isnan(sphere.Center.Y));
        EXPECT_TRUE(std::isnan(sphere.Center.Z)); EXPECT_TRUE(std::isnan(sphere.Radius));
    };
    check(BoundingSphere::CreateFromPoints({Vector3(nan)}));
    check(BoundingSphere::CreateFromPoints({Vector3(nan),Vector3(-2,0,0),Vector3(2,0,0)}));
    const auto finite=BoundingSphere::CreateFromPoints({Vector3(-2,0,0),Vector3(2,0,0),Vector3(nan)});
    EXPECT_EQ(finite.Center,Vector3::Zero); EXPECT_EQ(finite.Radius,2);
}

TEST(Tq031SpherePointSeedTest, SingularFrustumSpherePropagatesAllNanCornerSeed)
{
    // XNA Frustum.SetMatrix zero-normal divisions (IL_0336..037e), then
    // Sphere.CreateFromFrustum IL_0014..001f -> CreateFromPoints IL_002a..0040.
    const auto sphere=BoundingSphere::CreateFromFrustum(BoundingFrustum(Matrix()));
    EXPECT_TRUE(std::isnan(sphere.Center.X)); EXPECT_TRUE(std::isnan(sphere.Center.Y));
    EXPECT_TRUE(std::isnan(sphere.Center.Z)); EXPECT_TRUE(std::isnan(sphere.Radius));
}
