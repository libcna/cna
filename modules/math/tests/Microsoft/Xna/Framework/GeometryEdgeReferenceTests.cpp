// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "System/ArgumentException.hpp"

using namespace Microsoft::Xna::Framework;

TEST(GeometryEdgeReferenceTest, FrustumCoplanarAndZeroPlaneUseTheNonpositiveSide)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    for (const Plane plane : {Plane(Vector3::Zero, 0), Plane(Vector3::UnitZ, -1)})
    {
        PlaneIntersectionType output;
        frustum.Intersects(plane, output);
        EXPECT_EQ(static_cast<int>(output), 1);
        EXPECT_EQ(static_cast<int>(frustum.Intersects(plane)), 1);
        EXPECT_EQ(static_cast<int>(plane.Intersects(frustum)), 1);
    }
}

TEST(GeometryEdgeReferenceTest, PlaneDotsAndTransformsHaveLiteralOutputs)
{
    const Plane plane(Vector3(0, 2, 0), -4);
    const Vector3 point(1, -2, 3);
    const Vector4 homogeneous(point, 2);
    float output;
    EXPECT_EQ(plane.Dot(homogeneous), -12);
    plane.Dot(homogeneous, output); EXPECT_EQ(output, -12);
    EXPECT_EQ(plane.DotCoordinate(point), -8);
    plane.DotCoordinate(point, output); EXPECT_EQ(output, -8);
    EXPECT_EQ(plane.DotNormal(point), -4);
    plane.DotNormal(point, output); EXPECT_EQ(output, -4);
    const Matrix matrix(2, 0, 0, 0, 0, 4, 0, 0, 0, 0, -1, 0, 7, 8, 9, 1);
    const Plane expected(Vector3(0, 0.5f, 0), -8);
    Plane transformed;
    Plane::Transform(plane, matrix, transformed); EXPECT_EQ(transformed, expected);
    EXPECT_EQ(Plane::Transform(plane, matrix), expected);
    auto alias = plane; Plane::Transform(alias, matrix, alias); EXPECT_EQ(alias, expected);
    const Quaternion quaternion(0.5f, -0.5f, 0.5f, -0.5f);
    const Plane rotated(Vector3(0, 0, -2), -4);
    Plane::Transform(plane, quaternion, transformed); EXPECT_EQ(transformed, rotated);
    EXPECT_EQ(Plane::Transform(plane, quaternion), rotated);
    alias = plane; Plane::Transform(alias, quaternion, alias); EXPECT_EQ(alias, rotated);
}

TEST(GeometryEdgeReferenceTest, PlaneNormalizationTestsEachOutputAndAliasForm)
{
    const Plane plane(Vector3(0, 2, 0), -4), expected(Vector3::Up, -2);
    EXPECT_EQ(Plane::Normalize(plane), expected);
    Plane output;
    Plane::Normalize(plane, output); EXPECT_EQ(output, expected);
    auto alias = plane; Plane::Normalize(alias, alias); EXPECT_EQ(alias, expected);
    alias = plane; alias.Normalize(); EXPECT_EQ(alias, expected);
    const Plane tiny(Vector3(1e-30f, 0, 0), -1);
    const auto normalized = Plane::Normalize(tiny);
    EXPECT_EQ(normalized.Normal, Vector3::UnitX);
    EXPECT_EQ(normalized.D, -1.0000000150474662e30f);
}

TEST(GeometryEdgeReferenceTest, BoxContainmentAndIntersectionIncludeTouchingFaces)
{
    const BoundingBox box(Vector3(-1), Vector3(1)), touching(Vector3(1, -1, -1), Vector3(2, 1, 1));
    const BoundingSphere sphere(Vector3::Zero, 1);
    const Vector3 point(1, 0, 0);
    ContainmentType containment;
    bool hit;
    box.Contains(touching, containment); EXPECT_EQ(containment, ContainmentType::Intersects);
    EXPECT_EQ(box.Contains(touching), ContainmentType::Intersects);
    box.Contains(point, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(box.Contains(point), ContainmentType::Contains);
    box.Contains(sphere, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(box.Contains(sphere), ContainmentType::Contains);
    box.Intersects(touching, hit); EXPECT_TRUE(hit); EXPECT_TRUE(box.Intersects(touching));
    box.Intersects(sphere, hit); EXPECT_TRUE(hit); EXPECT_TRUE(box.Intersects(sphere));
    const Plane plane(Vector3::Up, 0);
    PlaneIntersectionType side;
    box.Intersects(plane, side); EXPECT_EQ(side, PlaneIntersectionType::Intersecting);
    EXPECT_EQ(box.Intersects(plane), PlaneIntersectionType::Intersecting);
    plane.Intersects(box, side); EXPECT_EQ(side, PlaneIntersectionType::Intersecting);
    EXPECT_EQ(plane.Intersects(box), PlaneIntersectionType::Intersecting);
}

TEST(GeometryEdgeReferenceTest, SphereContractsKeepStrictPointsAndExternalTangency)
{
    const BoundingSphere sphere(Vector3::Zero, 1), tangent(Vector3(2, 0, 0), 1);
    const BoundingSphere inner(Vector3::Zero, 0.5f);
    const BoundingBox contained(Vector3(-0.25f), Vector3(0.25f));
    ContainmentType containment;
    bool hit;
    sphere.Contains(Vector3::Right, containment); EXPECT_EQ(containment, ContainmentType::Disjoint);
    EXPECT_EQ(sphere.Contains(Vector3::Right), ContainmentType::Disjoint);
    sphere.Contains(inner, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(sphere.Contains(inner), ContainmentType::Contains);
    sphere.Contains(contained, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(sphere.Contains(contained), ContainmentType::Contains);
    sphere.Intersects(tangent, hit); EXPECT_FALSE(hit); EXPECT_FALSE(sphere.Intersects(tangent));
    sphere.Intersects(contained, hit); EXPECT_TRUE(hit); EXPECT_TRUE(sphere.Intersects(contained));
    const Plane plane(Vector3::Up, 2);
    PlaneIntersectionType side;
    sphere.Intersects(plane, side); EXPECT_EQ(side, PlaneIntersectionType::Front);
    EXPECT_EQ(sphere.Intersects(plane), PlaneIntersectionType::Front);
    plane.Intersects(sphere, side); EXPECT_EQ(side, PlaneIntersectionType::Front);
    EXPECT_EQ(plane.Intersects(sphere), PlaneIntersectionType::Front);
}

TEST(GeometryEdgeReferenceTest, RayBoxThresholdAndStationaryOriginsRetainMeasuredXnaDecision)
{
    const BoundingBox box(Vector3(-1), Vector3(1));
    const Ray stationary(Vector3::Zero, Vector3::Zero);
    std::optional<float> result;
    stationary.Intersects(box, result); EXPECT_EQ(result, 0);
    EXPECT_EQ(stationary.Intersects(box), 0);
    box.Intersects(stationary, result); EXPECT_EQ(result, 0);
    EXPECT_EQ(box.Intersects(stationary), 0);
    const Ray parallel(Vector3(-2, 0, 0), Vector3(0.5e-6f, 0, 0));
    parallel.Intersects(box, result); EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(parallel.Intersects(box).has_value());
    const Ray entry(Vector3(-2, 0, 0), Vector3(1e-6f, 0, 0));
    entry.Intersects(box, result); ASSERT_TRUE(result.has_value()); EXPECT_EQ(*result, 1000000);
    EXPECT_EQ(entry.Intersects(box), 1000000);
}

TEST(GeometryEdgeReferenceTest, RaySphereAndPlaneParametersDoNotNormalizeDirection)
{
    const Ray ray(Vector3(-3, 0, 0), Vector3::Right);
    const BoundingSphere sphere(Vector3::Zero, 1);
    std::optional<float> output;
    ray.Intersects(sphere, output); EXPECT_EQ(output, 2);
    EXPECT_EQ(ray.Intersects(sphere), 2);
    sphere.Intersects(ray, output); EXPECT_EQ(output, 2);
    EXPECT_EQ(sphere.Intersects(ray), 2);
    const Plane plane(Vector3::Right, 0);
    const Ray nonunit(Vector3(-3, 0, 0), Vector3(2, 0, 0));
    nonunit.Intersects(plane, output); EXPECT_EQ(output, 1.5f);
    EXPECT_EQ(nonunit.Intersects(plane), 1.5f);
    const Ray parallel(Vector3(-3, 0, 0), Vector3(0.9e-5f, 0, 0));
    parallel.Intersects(plane, output); EXPECT_FALSE(output.has_value());
    EXPECT_FALSE(parallel.Intersects(plane).has_value());
}

TEST(GeometryEdgeReferenceTest, FrustumVolumeRelationsAndPointToleranceUseLiteralGeometry)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const BoundingBox box(Vector3(-0.25f, -0.25f, 0.25f), Vector3(0.25f, 0.25f, 0.75f));
    const BoundingSphere sphere(Vector3(0, 0, 0.5f), 0.25f);
    ContainmentType containment;
    bool hit;
    frustum.Contains(box, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(box), ContainmentType::Contains);
    frustum.Contains(sphere, containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(sphere), ContainmentType::Contains);
    frustum.Contains(Vector3(0, 0, -0.5e-5f), containment); EXPECT_EQ(containment, ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(Vector3(0, 0, -0.5e-5f)), ContainmentType::Contains);
    frustum.Intersects(box, hit); EXPECT_TRUE(hit); EXPECT_TRUE(frustum.Intersects(box));
    frustum.Intersects(sphere, hit); EXPECT_TRUE(hit); EXPECT_TRUE(frustum.Intersects(sphere));
    EXPECT_TRUE(box.Intersects(frustum)); EXPECT_TRUE(sphere.Intersects(frustum));
    EXPECT_EQ(box.Contains(frustum), ContainmentType::Intersects);
    EXPECT_EQ(sphere.Contains(frustum), ContainmentType::Intersects);
    const BoundingBox distant(Vector3(3), Vector3(4));
    const BoundingSphere distantSphere(Vector3(5), 1);
    EXPECT_EQ(distant.Contains(frustum), ContainmentType::Disjoint);
    EXPECT_EQ(distantSphere.Contains(frustum), ContainmentType::Disjoint);
    EXPECT_TRUE(frustum.Intersects(frustum));
    EXPECT_EQ(frustum.Contains(frustum), ContainmentType::Contains);
}

TEST(GeometryEdgeReferenceTest, PointConstructionAndMergedVolumesHaveLiteralExtents)
{
    const std::vector<Vector3> points{{-2, 0, 0}, {2, 0, 0}};
    const BoundingBox expected(Vector3(-2, 0, 0), Vector3(2, 0, 0));
    EXPECT_EQ(BoundingBox::CreateFromPoints(points), expected);
    const BoundingSphere sphere = BoundingSphere::CreateFromPoints(points);
    EXPECT_EQ(sphere.Center, Vector3::Zero); EXPECT_EQ(sphere.Radius, 2);
    EXPECT_THROW((void)BoundingBox::CreateFromPoints({}), System::ArgumentException);
    EXPECT_THROW((void)BoundingSphere::CreateFromPoints({}), System::ArgumentException);
    const BoundingBox other(Vector3(-1), Vector3(1));
    const BoundingBox merged(Vector3(-2, -1, -1), Vector3(2, 1, 1));
    BoundingBox output;
    BoundingBox::CreateMerged(expected, other, output); EXPECT_EQ(output, merged);
    EXPECT_EQ(BoundingBox::CreateMerged(expected, other), merged);
    const BoundingSphere a(Vector3::Zero, 1), b(Vector3(2, 0, 0), 1);
    BoundingSphere sphereOutput;
    BoundingSphere::CreateMerged(a, b, sphereOutput);
    EXPECT_EQ(sphereOutput.Center, Vector3(1, 0, 0)); EXPECT_EQ(sphereOutput.Radius, 2);
    const auto sphereMerged = BoundingSphere::CreateMerged(a, b);
    EXPECT_EQ(sphereMerged.Center, Vector3(1, 0, 0)); EXPECT_EQ(sphereMerged.Radius, 2);
    BoundingBox::CreateFromSphere(a, output); EXPECT_EQ(output, other);
    EXPECT_EQ(BoundingBox::CreateFromSphere(a), other);
    const BoundingBox line(Vector3(-3, 0, 0), Vector3(3, 0, 0));
    BoundingSphere::CreateFromBoundingBox(line, sphereOutput);
    EXPECT_EQ(sphereOutput.Center, Vector3::Zero); EXPECT_EQ(sphereOutput.Radius, 3);
    const auto fromBox = BoundingSphere::CreateFromBoundingBox(line);
    EXPECT_EQ(fromBox.Center, Vector3::Zero); EXPECT_EQ(fromBox.Radius, 3);
}

TEST(GeometryEdgeReferenceTest, FrustumCornersAndSphereUseIndependentFrozenLiterals)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const std::vector<Vector3> expected{{-1, 1, 0}, {1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
        {-1, 1, 1}, {1, 1, 1}, {1, -1, 1}, {-1, -1, 1}};
    EXPECT_EQ(frustum.GetCorners(), expected);
    std::vector<Vector3> corners(8); frustum.GetCorners(corners); EXPECT_EQ(corners, expected);
    const auto sphere = BoundingSphere::CreateFromFrustum(frustum);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sphere.Center.X), 0xbc4deb64u);
    EXPECT_EQ(sphere.Radius, 1.7012073993682861f);
}
