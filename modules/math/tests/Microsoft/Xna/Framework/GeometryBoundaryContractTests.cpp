// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"

using namespace Microsoft::Xna::Framework;

TEST(GeometryBoundaryContractTest, BoxSphereFaceEdgeAndCornerDistancesUseAllAxesAndIncludeTangency)
{
    // FNA BoundingBox.Contains/Intersects: nearest-point squared distance <= r^2.
    // At face/edge/corner offsets (1,0,0),(1,1,0),(1,2,2), squared distances are 1,2,9.
    const BoundingBox box(Vector3(-1), Vector3(1));
    struct Sample { Vector3 center; float radius; ContainmentType contains; bool hit; };
    const Sample cases[] = {
        {{0,0,0},1,ContainmentType::Contains,true},
        {{2,0,0},1,ContainmentType::Intersects,true},
        {{2,2,0},1,ContainmentType::Disjoint,false},
        {{2,2,0},1.5f,ContainmentType::Intersects,true},
        {{2,3,3},3,ContainmentType::Intersects,true},
        {{2,3,3},2.5f,ContainmentType::Disjoint,false},
        {{5,0,0},1,ContainmentType::Disjoint,false},
        {{1,0,0},0,ContainmentType::Contains,true},
    };
    for (const auto& sample : cases)
        for (int permutation=0;permutation<3;++permutation)
            for (float sign : {-1.0f,1.0f})
            {
                const auto v=sample.center;
                const Vector3 center = permutation==0 ? v : permutation==1 ? Vector3(v.Y,v.Z,v.X) : Vector3(v.Z,v.X,v.Y);
                const BoundingSphere sphere(center*sign,sample.radius);
                SCOPED_TRACE(sphere.ToString());
                ContainmentType containment=ContainmentType::Disjoint;
                box.Contains(sphere,containment);
                EXPECT_EQ(containment,sample.contains);
                EXPECT_EQ(box.Contains(sphere),sample.contains);
                bool hit=!sample.hit;
                box.Intersects(sphere,hit);
                EXPECT_EQ(hit,sample.hit);
                EXPECT_EQ(box.Intersects(sphere),sample.hit);
                sphere.Intersects(box,hit);
                EXPECT_EQ(hit,sample.hit);
                EXPECT_EQ(sphere.Intersects(box),sample.hit);
            }
}

TEST(GeometryBoundaryContractTest, PlaneBoxAndSphereClassificationsIncludeExactContactOnEveryAxis)
{
    // FNA BoundingBox/BoundingSphere.Intersects(Plane): strict >/< sides;
    // planes at either support boundary still intersect the closed volume.
    const BoundingBox box(Vector3(-1),Vector3(1));
    const BoundingSphere sphere(Vector3::Zero,1);
    for (const auto axis : {Vector3::UnitX,Vector3::UnitY,Vector3::UnitZ})
        for (float sign : {-1.0f,1.0f})
            for (float distance : {-2.0f,-1.0f,0.0f,1.0f,2.0f})
            {
                const Plane plane(axis*sign,distance);
                const auto expected = distance>1 ? PlaneIntersectionType::Front :
                    distance < -1 ? PlaneIntersectionType::Back : PlaneIntersectionType::Intersecting;
                PlaneIntersectionType output=PlaneIntersectionType::Front;
                box.Intersects(plane,output);
                EXPECT_EQ(output,expected);
                EXPECT_EQ(box.Intersects(plane),expected);
                plane.Intersects(box,output);
                EXPECT_EQ(output,expected);
                EXPECT_EQ(plane.Intersects(box),expected);
                sphere.Intersects(plane,output);
                EXPECT_EQ(output,expected);
                EXPECT_EQ(sphere.Intersects(plane),expected);
                plane.Intersects(sphere,output);
                EXPECT_EQ(output,expected);
                EXPECT_EQ(plane.Intersects(sphere),expected);
            }
}

TEST(GeometryBoundaryContractTest, CollinearPlanePointsPropagateUndefinedNormalAndDistance)
{
    // FNA Plane(a,b,c): normalize((b-a) cross (c-a)); zero cross product gives NaN.
    for (const auto first : {Vector3::Zero,Vector3(7,8,9)})
    {
        const Plane plane(first,first+Vector3(1,2,3),first+Vector3(2,4,6));
        EXPECT_TRUE(std::isnan(plane.Normal.X));
        EXPECT_TRUE(std::isnan(plane.Normal.Y));
        EXPECT_TRUE(std::isnan(plane.Normal.Z));
        EXPECT_TRUE(std::isnan(plane.D));
    }
}

TEST(GeometryBoundaryContractTest, SphereTransformUsesLongestRowAndIsSafeWhenOutputAliasesInput)
{
    // FNA BoundingSphere.Transform: largest squared ROW length scales radius, not column length.
    // Row lengths are 5,12,2 (columns have lengths 3,sqrt(160),2).
    const Matrix matrix(3,4,0,0, 0,12,0,0, 0,0,-2,0, 7,8,9,1);
    const BoundingSphere source(Vector3(1,2,3),2);
    const BoundingSphere expected(Vector3(10,36,3),24); // (1,2,3)*M; 2*12.
    const auto value=source.Transform(matrix);
    EXPECT_EQ(value.Center,expected.Center);
    EXPECT_EQ(value.Radius,expected.Radius);
    BoundingSphere output;
    source.Transform(matrix,output);
    EXPECT_EQ(output.Center,expected.Center);
    EXPECT_EQ(output.Radius,expected.Radius);
    auto alias=source;
    alias.Transform(matrix,alias);
    EXPECT_EQ(alias.Center,expected.Center);
    EXPECT_EQ(alias.Radius,expected.Radius);
    EXPECT_EQ(source.Center,Vector3(1,2,3));
    EXPECT_EQ(source.Radius,2);
}

TEST(GeometryBoundaryContractTest, MergedSpheresHandleContainmentAndUnequalRadiiWithEitherInputAliased)
{
    // FNA BoundingSphere.CreateMerged: contained spheres are returned verbatim.
    // Disjoint (center 0,r=1) and (center 6,r=3) span [-1,9]: center=4,r=5.
    const BoundingSphere small(Vector3::Zero,1), large(Vector3(6,0,0),3);
    struct Sample { BoundingSphere first,second,expected; };
    const Sample cases[] = {
        {small,large,{Vector3(4,0,0),5}},
        {large,small,{Vector3(4,0,0),5}},
        {{Vector3::Zero,5},{Vector3(2,0,0),3},{Vector3::Zero,5}},
        {{Vector3(2,0,0),3},{Vector3::Zero,5},{Vector3::Zero,5}},
        {small,small,small},
    };
    for (const auto& sample : cases)
    {
        EXPECT_EQ(BoundingSphere::CreateMerged(sample.first,sample.second),sample.expected);
        BoundingSphere output;
        BoundingSphere::CreateMerged(sample.first,sample.second,output);
        EXPECT_EQ(output,sample.expected);
        auto alias=sample.first;
        BoundingSphere::CreateMerged(alias,sample.second,alias);
        EXPECT_EQ(alias,sample.expected);
        alias=sample.second;
        BoundingSphere::CreateMerged(sample.first,alias,alias);
        EXPECT_EQ(alias,sample.expected);
    }
}

TEST(GeometryBoundaryContractTest, FrustumMatrixSetterRebuildsCornersAndContainmentTogether)
{
    // Clip x,y in [-1,1], z in [0,1]. Setting M41=-4 translates world bounds by +4 X.
    BoundingFrustum frustum(Matrix::getIdentityProperty());
    Matrix shifted(1,0,0,0, 0,1,0,0, 0,0,1,0, -4,0,0,1);
    frustum.setMatrixProperty(shifted);
    EXPECT_EQ(frustum.getMatrixProperty(),shifted);
    const std::vector<Vector3> expected{{3,1,0},{5,1,0},{5,-1,0},{3,-1,0},
        {3,1,1},{5,1,1},{5,-1,1},{3,-1,1}};
    EXPECT_EQ(frustum.GetCorners(),expected);
    std::vector<Vector3> filled(10,Vector3(99));
    frustum.GetCorners(filled);
    for (std::size_t i=0;i<8;++i) EXPECT_EQ(filled[i],expected[i]);
    EXPECT_EQ(filled[8],Vector3(99));
    EXPECT_EQ(filled[9],Vector3(99));
    EXPECT_EQ(frustum.Contains(Vector3(4,0,0.5f)),ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(Vector3(0,0,0.5f)),ContainmentType::Disjoint);
    EXPECT_EQ(frustum.getLeftProperty().Normal,Vector3(-1,0,0));
    EXPECT_EQ(frustum.getLeftProperty().D,3);
    EXPECT_EQ(frustum.getRightProperty().Normal,Vector3(1,0,0));
    EXPECT_EQ(frustum.getRightProperty().D,-5);
}
