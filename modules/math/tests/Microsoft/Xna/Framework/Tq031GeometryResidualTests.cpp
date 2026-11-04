// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <limits>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031GeometryResidualTest, PointAndZeroSizeVolumesDifferAtEveryFrustumFace)
{
    // XNA Frustum.Contains(point) IL_0060..0077 accepts exact planes; Contains(box)
    // IL_0024..0043 records Intersecting plane; Contains(sphere) IL_0082..009d
    // requires STRICTLY negative distance for all six planes even for radius zero.
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const Vector3 points[]={{-1,0,.5f},{1,0,.5f},{0,-1,.5f},{0,1,.5f},{0,0,0},{0,0,1}};
    const Vector3 normals[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    for (std::size_t i=0;i<6;++i)
    {
        ContainmentType output=ContainmentType::Disjoint;
        frustum.Contains(points[i],output); EXPECT_EQ(output,ContainmentType::Contains);
        EXPECT_EQ(frustum.Contains(points[i]),ContainmentType::Contains);
        for (float offset : {-0.25f,0.0f,0.25f})
        {
            const Vector3 point=points[i]+normals[i]*offset;
            const auto expected=offset<0 ? ContainmentType::Contains :
                offset>0 ? ContainmentType::Disjoint : ContainmentType::Intersects;
            const BoundingBox box(point,point); const BoundingSphere sphere(point,0);
            output=expected==ContainmentType::Contains ? ContainmentType::Disjoint : ContainmentType::Contains;
            frustum.Contains(box,output); EXPECT_EQ(output,expected);
            EXPECT_EQ(frustum.Contains(box),expected);
            output=expected==ContainmentType::Contains ? ContainmentType::Disjoint : ContainmentType::Contains;
            frustum.Contains(sphere,output); EXPECT_EQ(output,expected);
            EXPECT_EQ(frustum.Contains(sphere),expected);
        }
    }
}

TEST(Tq031GeometryResidualTest, NanPlaneUsesFrustumBackButBoxAndSphereIntersecting)
{
    // XNA Frustum.Intersects(Plane) ble.un IL_002e selects nonpositive side.
    // Plane.Intersects(Box) IL_017b/01c8 and Sphere IL_005c/0069 skip both strict sides.
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const BoundingBox box(Vector3(-1),Vector3(1)); const BoundingSphere sphere(Vector3::Zero,1);
    for (const Plane plane : {Plane(nan,0,0,0),Plane(0,0,0,nan)})
    {
        PlaneIntersectionType output=PlaneIntersectionType::Front;
        frustum.Intersects(plane,output); EXPECT_EQ(output,PlaneIntersectionType::Back);
        EXPECT_EQ(frustum.Intersects(plane),PlaneIntersectionType::Back);
        EXPECT_EQ(plane.Intersects(frustum),PlaneIntersectionType::Back);
        output=PlaneIntersectionType::Front; plane.Intersects(box,output);
        EXPECT_EQ(output,PlaneIntersectionType::Intersecting);
        EXPECT_EQ(plane.Intersects(box),PlaneIntersectionType::Intersecting);
        output=PlaneIntersectionType::Front; box.Intersects(plane,output);
        EXPECT_EQ(output,PlaneIntersectionType::Intersecting);
        EXPECT_EQ(box.Intersects(plane),PlaneIntersectionType::Intersecting);
        output=PlaneIntersectionType::Front; plane.Intersects(sphere,output);
        EXPECT_EQ(output,PlaneIntersectionType::Intersecting);
        EXPECT_EQ(plane.Intersects(sphere),PlaneIntersectionType::Intersecting);
        output=PlaneIntersectionType::Front; sphere.Intersects(plane,output);
        EXPECT_EQ(output,PlaneIntersectionType::Intersecting);
        EXPECT_EQ(sphere.Intersects(plane),PlaneIntersectionType::Intersecting);
    }
}

TEST(Tq031GeometryResidualTest, ShortFrustumCornerDestinationIsUnchangedOnTypedFailure)
{
    // XNA Frustum.GetCorners(array) IL_000e..0023 validates length before Array.CopyTo.
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    for (std::size_t size=0;size<8;++size)
    {
        std::vector<Vector3> destination(size,Vector3(99));
        EXPECT_THROW(frustum.GetCorners(destination),System::ArgumentOutOfRangeException);
        EXPECT_EQ(destination,std::vector<Vector3>(size,Vector3(99)));
    }
}

TEST(Tq031GeometryResidualTest, NanGeometryValueIsUnequalToItsBoxedCopy)
{
    // XNA Plane.Equals IL_0000..005c, Sphere.Equals IL_0000..0025,
    // Box.Equals IL_0000..0028, Ray.Equals IL_0000..009b use ordered field equality.
    // Equals(object) unboxes and delegates; NaN never compares equal, even to its copy.
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const auto check=[](const auto& value) {
        const auto copy=value;
        EXPECT_FALSE(value.Equals(copy)); EXPECT_FALSE(value.Equals(std::any(copy)));
        EXPECT_FALSE(value==copy); EXPECT_TRUE(value!=copy);
    };
    check(Plane(Vector3(nan,0,0),1)); check(Plane(Vector3::UnitX,nan));
    check(BoundingSphere(Vector3(nan,0,0),1)); check(BoundingSphere(Vector3::Zero,nan));
    check(BoundingBox(Vector3(nan,0,0),Vector3(1)));
    check(BoundingBox(Vector3::Zero,Vector3(1,nan,1)));
    check(Ray(Vector3(nan,0,0),Vector3::UnitX)); check(Ray(Vector3::Zero,Vector3(0,nan,0)));
    // XNA Frustum.Equals(Frustum) IL_000b..001c delegates matrix equality;
    // distinct copies avoid the CLR reference-identity shortcut in operator==.
    Matrix matrix=Matrix::getIdentityProperty(); matrix.M11=nan;
    const BoundingFrustum first(matrix),second(matrix);
    EXPECT_FALSE(first.Equals(second)); EXPECT_FALSE(first.Equals(std::any(second)));
    EXPECT_FALSE(first==second); EXPECT_TRUE(first!=second);
}
