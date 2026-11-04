// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031SphereFactoryTest, BoxFactoryUsesHalfEndpointDistanceDespiteRoundedMidpoint)
{
    // XNA Sphere.CreateFromBoundingBox value IL_0013..003d/out IL_0017..0037:
    // center=Lerp(Min,Max,.5), radius=Distance(Min,Max)*.5, independently.
    // 2^24 and 2^24+2 differ by 2; midpoint 2^24+1 ties to even 2^24, radius remains 1.
    for (const auto axis : {Vector3::UnitX,Vector3::UnitY,Vector3::UnitZ})
    {
        const BoundingBox box(axis*16777216,axis*16777218);
        const auto value=BoundingSphere::CreateFromBoundingBox(box);
        EXPECT_EQ(value.Center,axis*16777216); EXPECT_EQ(value.Radius,1);
        BoundingSphere output(Vector3(99),99);
        BoundingSphere::CreateFromBoundingBox(box,output);
        EXPECT_EQ(output.Center,axis*16777216); EXPECT_EQ(output.Radius,1);
    }
}

