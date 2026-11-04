// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031FrustumDegeneracyTest, PlaneDistanceIsDividedByLengthWithoutRoundedReciprocal)
{
    // XNA Frustum.SetMatrix IL_0347..037e: Normal uses vector division;
    // D uses direct scalar division. round32(-5/3)=0xbfd55555, not 0xbfd55556.
    const Matrix matrix(3,0,0,0, 0,3,0,0, 0,0,3,0, 0,0,0,5);
    BoundingFrustum frustum(Matrix::getIdentityProperty());
    frustum.setMatrixProperty(matrix);
    const BoundingFrustum constructed(matrix);
    const BoundingFrustum* values[] = {&frustum,&constructed};
    for (const auto* value : values)
    {
        EXPECT_EQ(value->getNearProperty().Normal,Vector3(0,0,-1));
        EXPECT_EQ(value->getNearProperty().D,0);
        for (const auto plane : {value->getFarProperty(),value->getLeftProperty(),value->getRightProperty(),
                                value->getTopProperty(),value->getBottomProperty()})
            EXPECT_EQ(std::bit_cast<std::uint32_t>(plane.D),0xbfd55555u);
        EXPECT_EQ(value->getLeftProperty().Normal,Vector3(-1,0,0));
        EXPECT_EQ(value->getRightProperty().Normal,Vector3(1,0,0));
        EXPECT_EQ(value->getTopProperty().Normal,Vector3(0,1,0));
        EXPECT_EQ(value->getBottomProperty().Normal,Vector3(0,-1,0));
        EXPECT_EQ(value->getFarProperty().Normal,Vector3(0,0,1));
    }
}

TEST(Tq031FrustumDegeneracyTest, ZeroMatrixPropagatesNanPlanesCornersAndContainment)
{
    // XNA SetMatrix IL_0336..0389 divides all zero plane coefficients by length 0.
    // ComputeIntersectionLine IL_0016..0069, ComputeIntersection IL_0013..0043
    // propagate NaN to every corner. Contains(point) ble.un at IL_0066 accepts NaN;
    // Contains(sphere) neither side test counts an inside plane => Intersects.
    const BoundingFrustum frustum{Matrix()};
    for (const auto plane : {frustum.getNearProperty(),frustum.getFarProperty(),frustum.getLeftProperty(),
                            frustum.getRightProperty(),frustum.getTopProperty(),frustum.getBottomProperty()})
    {
        EXPECT_TRUE(std::isnan(plane.Normal.X)); EXPECT_TRUE(std::isnan(plane.Normal.Y));
        EXPECT_TRUE(std::isnan(plane.Normal.Z)); EXPECT_TRUE(std::isnan(plane.D));
    }
    const auto returned=frustum.GetCorners(); ASSERT_EQ(returned.size(),8u);
    std::vector<Vector3> copied(10,Vector3(99)); frustum.GetCorners(copied);
    for (std::size_t i=0;i<8;++i)
        for (const auto corner : {returned[i],copied[i]})
        {
            EXPECT_TRUE(std::isnan(corner.X)); EXPECT_TRUE(std::isnan(corner.Y)); EXPECT_TRUE(std::isnan(corner.Z));
        }
    EXPECT_EQ(copied[8],Vector3(99)); EXPECT_EQ(copied[9],Vector3(99));
    ContainmentType output=ContainmentType::Disjoint;
    frustum.Contains(Vector3(99),output); EXPECT_EQ(output,ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(Vector3(99)),ContainmentType::Contains);
    const BoundingSphere sphere(Vector3::Zero,1);
    output=ContainmentType::Contains; frustum.Contains(sphere,output);
    EXPECT_EQ(output,ContainmentType::Intersects);
    EXPECT_EQ(frustum.Contains(sphere),ContainmentType::Intersects);
}

TEST(Tq031FrustumDegeneracyTest, NanPointAndSphereDoNotUseTheSameContainmentFallthrough)
{
    // XNA Frustum.Contains(point) IL_0060..0077: all unordered distances pass.
    // Contains(sphere) IL_0078..009d: zero inside-plane count yields Intersects.
    // Contrasts BoundingSphere.Contains(point)'s strict less-than (already covered elsewhere).
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const float nan=std::numeric_limits<float>::quiet_NaN();
    ContainmentType output=ContainmentType::Disjoint;
    frustum.Contains(Vector3(nan,0,0.5f),output); EXPECT_EQ(output,ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(Vector3(nan,0,0.5f)),ContainmentType::Contains);
    const BoundingSphere sphere(Vector3(0,0,0.5f),nan);
    output=ContainmentType::Contains; frustum.Contains(sphere,output);
    EXPECT_EQ(output,ContainmentType::Intersects);
    EXPECT_EQ(frustum.Contains(sphere),ContainmentType::Intersects);
}
