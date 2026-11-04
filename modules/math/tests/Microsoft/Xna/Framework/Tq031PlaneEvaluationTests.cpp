// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <limits>
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031PlaneEvaluationTest, DotProductsRetainCancellationUntilTheirSingleOutput)
{
    // XNA Plane.Dot IL_0012..004a, DotCoordinate IL_0012..0042,
    // DotNormal IL_0012..003b (out stores: 0047,0040,0039): left-associated
    // evaluation before the Single output. 2^24+1-2^24=1; adding D*W=6 gives 7.
    const Plane plane(16777216,1,-16777216,2);
    const Vector3 point(1,1,1);
    float output=99;
    EXPECT_EQ(plane.Dot(Vector4(point,3)),7);
    plane.Dot(Vector4(point,3),output); EXPECT_EQ(output,7);
    EXPECT_EQ(plane.DotCoordinate(point),3);
    plane.DotCoordinate(point,output); EXPECT_EQ(output,3);
    EXPECT_EQ(plane.DotNormal(point),1);
    plane.DotNormal(point,output); EXPECT_EQ(output,1);
    auto alias=point;
    plane.DotNormal(alias,alias.Y); EXPECT_EQ(alias.Y,1);
}

TEST(Tq031PlaneEvaluationTest, FrustumPlaneClassificationStoresDotBeforeAddingD)
{
    // Clip x,y in [1,3], z in [0,1]. XNA Frustum.Intersects(Plane)
    // IL_001b stores Vector3.Dot through an out float; IL_0020..002e then adds D.
    // At x=y=1, dot=2^24+1 rounds to 2^24, so d=0 => nonpositive side.
    // Other corners are positive: Intersecting, whereas a single wide dot+D is Front.
    const Matrix matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, -2,-2,0,1);
    const BoundingFrustum frustum(matrix);
    const Plane plane(16777216,1,0,-16777216);
    PlaneIntersectionType output=PlaneIntersectionType::Front;
    frustum.Intersects(plane,output);
    EXPECT_EQ(output,PlaneIntersectionType::Intersecting);
    EXPECT_EQ(frustum.Intersects(plane),PlaneIntersectionType::Intersecting);
    EXPECT_EQ(plane.Intersects(frustum),PlaneIntersectionType::Intersecting);
}
