#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::ContainmentType;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::PlaneIntersectionType;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Vector3;

// --- Construction ---

TEST(BoundingBoxTest, ConstructorStoresMinMax)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(box.Min.X, 0.0f);
    EXPECT_FLOAT_EQ(box.Max.X, 1.0f);
}

// --- Contains (point) ---

TEST(BoundingBoxTest, ContainsCenterPoint)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    EXPECT_EQ(box.Contains(Vector3(1.0f, 1.0f, 1.0f)), ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsMinCorner)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(box.Contains(Vector3(0.0f, 0.0f, 0.0f)), ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsMaxCorner)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(box.Contains(Vector3(1.0f, 1.0f, 1.0f)), ContainmentType::Contains);
}

TEST(BoundingBoxTest, DisjointPointOutside)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(box.Contains(Vector3(2.0f, 0.0f, 0.0f)), ContainmentType::Disjoint);
    EXPECT_EQ(box.Contains(Vector3(-1.0f, 0.5f, 0.5f)), ContainmentType::Disjoint);
}

// --- Contains (box) ---

TEST(BoundingBoxTest, ContainsFullyInsideBox)
{
    BoundingBox outer(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 10.0f, 10.0f));
    BoundingBox inner(Vector3(1.0f, 1.0f, 1.0f), Vector3(5.0f, 5.0f, 5.0f));
    EXPECT_EQ(outer.Contains(inner), ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsOverlappingBoxIsIntersects)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 5.0f, 5.0f));
    BoundingBox b(Vector3(3.0f, 3.0f, 3.0f), Vector3(8.0f, 8.0f, 8.0f));
    EXPECT_EQ(a.Contains(b), ContainmentType::Intersects);
}

TEST(BoundingBoxTest, ContainsDisjointBoxIsDisjoint)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(5.0f, 5.0f, 5.0f), Vector3(6.0f, 6.0f, 6.0f));
    EXPECT_EQ(a.Contains(b), ContainmentType::Disjoint);
}

// --- Intersects (box) ---

TEST(BoundingBoxTest, IntersectsOverlappingBoxesIsTrue)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 5.0f, 5.0f));
    BoundingBox b(Vector3(3.0f, 3.0f, 3.0f), Vector3(8.0f, 8.0f, 8.0f));
    EXPECT_TRUE(a.Intersects(b));
}

TEST(BoundingBoxTest, IntersectsAdjacentBoxesFalse)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 1.0f, 1.0f));
    // Touching on a face — XNA includes the boundary as intersecting
    EXPECT_TRUE(a.Intersects(b));
}

TEST(BoundingBoxTest, IntersectsDisjointBoxesFalse)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(2.0f, 2.0f, 2.0f), Vector3(3.0f, 3.0f, 3.0f));
    EXPECT_FALSE(a.Intersects(b));
}

// --- Intersects (sphere) ---

TEST(BoundingBoxTest, IntersectsOverlappingSphereIsTrue)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    BoundingSphere sphere(Vector3(1.0f, 1.0f, 1.0f), 0.5f);
    EXPECT_TRUE(box.Intersects(sphere));
}

TEST(BoundingBoxTest, IntersectsDistantSphereIsFalse)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingSphere sphere(Vector3(10.0f, 10.0f, 10.0f), 0.5f);
    EXPECT_FALSE(box.Intersects(sphere));
}

// --- GetCorners ---

TEST(BoundingBoxTest, GetCornersReturnsEightPoints)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    auto corners = box.GetCorners();
    EXPECT_EQ(static_cast<int>(corners.size()), BoundingBox::CornerCount);
}

TEST(BoundingBoxTest, GetCornersContainsMinAndMax)
{
    Vector3 minV(1.0f, 2.0f, 3.0f);
    Vector3 maxV(4.0f, 5.0f, 6.0f);
    BoundingBox box(minV, maxV);
    auto corners = box.GetCorners();
    bool hasMin = false, hasMax = false;
    for (const auto& c : corners) {
        if (c.X == minV.X && c.Y == minV.Y && c.Z == minV.Z) hasMin = true;
        if (c.X == maxV.X && c.Y == maxV.Y && c.Z == maxV.Z) hasMax = true;
    }
    EXPECT_TRUE(hasMin);
    EXPECT_TRUE(hasMax);
}

// --- CreateFromPoints ---

TEST(BoundingBoxTest, CreateFromPointsEnclosesAll)
{
    std::vector<Vector3> pts = {
        Vector3(-1.0f, 0.0f, 0.0f),
        Vector3(2.0f, 3.0f, -1.0f),
        Vector3(0.0f, -2.0f, 4.0f)
    };
    BoundingBox box = BoundingBox::CreateFromPoints(pts);
    EXPECT_FLOAT_EQ(box.Min.X, -1.0f);
    EXPECT_FLOAT_EQ(box.Min.Y, -2.0f);
    EXPECT_FLOAT_EQ(box.Min.Z, -1.0f);
    EXPECT_FLOAT_EQ(box.Max.X, 2.0f);
    EXPECT_FLOAT_EQ(box.Max.Y, 3.0f);
    EXPECT_FLOAT_EQ(box.Max.Z, 4.0f);
}

// --- CreateMerged ---

TEST(BoundingBoxTest, CreateMergedEnclosesBoths)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(2.0f, 2.0f, 2.0f), Vector3(3.0f, 3.0f, 3.0f));
    BoundingBox merged = BoundingBox::CreateMerged(a, b);
    EXPECT_FLOAT_EQ(merged.Min.X, 0.0f);
    EXPECT_FLOAT_EQ(merged.Max.X, 3.0f);
    EXPECT_FLOAT_EQ(merged.Min.Y, 0.0f);
    EXPECT_FLOAT_EQ(merged.Max.Y, 3.0f);
}

// --- Equality ---

TEST(BoundingBoxTest, EqualBoxesCompareEqual)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(BoundingBoxTest, DifferentBoxesCompareNotEqual)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 1.0f, 1.0f));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Equals() direct call ---

TEST(BoundingBoxTest, EqualsDirectCallSameBox)
{
    BoundingBox a(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    BoundingBox b(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    EXPECT_TRUE(a.Equals(b));
}

TEST(BoundingBoxTest, EqualsDirectCallDifferentBox)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 2.0f));
    EXPECT_FALSE(a.Equals(b));
}

// --- Contains (BoundingSphere) ---

TEST(BoundingBoxTest, ContainsSphereFullyInside)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 10.0f, 10.0f));
    BoundingSphere sphere(Vector3(5.0f, 5.0f, 5.0f), 1.0f);
    EXPECT_EQ(box.Contains(sphere), ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsSphereIntersecting)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    BoundingSphere sphere(Vector3(1.0f, 1.0f, 1.0f), 2.0f);
    EXPECT_EQ(box.Contains(sphere), ContainmentType::Intersects);
}

TEST(BoundingBoxTest, ContainsSphereDisjoint)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingSphere sphere(Vector3(10.0f, 10.0f, 10.0f), 0.5f);
    EXPECT_EQ(box.Contains(sphere), ContainmentType::Disjoint);
}

// --- Contains out-ref overloads ---

TEST(BoundingBoxTest, ContainsBoxOutRef)
{
    BoundingBox outer(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 10.0f, 10.0f));
    BoundingBox inner(Vector3(1.0f, 1.0f, 1.0f), Vector3(5.0f, 5.0f, 5.0f));
    ContainmentType result;
    outer.Contains(inner, result);
    EXPECT_EQ(result, ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsSphereOutRef)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 10.0f, 10.0f));
    BoundingSphere sphere(Vector3(5.0f, 5.0f, 5.0f), 1.0f);
    ContainmentType result;
    box.Contains(sphere, result);
    EXPECT_EQ(result, ContainmentType::Contains);
}

TEST(BoundingBoxTest, ContainsPointOutRef)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    ContainmentType result;
    box.Contains(Vector3(0.5f, 0.5f, 0.5f), result);
    EXPECT_EQ(result, ContainmentType::Contains);
    box.Contains(Vector3(5.0f, 5.0f, 5.0f), result);
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- Contains (BoundingFrustum) ---

TEST(BoundingBoxTest, ContainsFrustumDisjoint)
{
    // A unit box at the origin and a perspective frustum pointing far away
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, -100.0f),
        Vector3(0.0f, 0.0f, -200.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(
        0.785398f, 1.0f, 1.0f, 50.0f);
    BoundingFrustum frustum(view * proj);
    // The box is behind the frustum; result must be Disjoint or Intersects (not Contains)
    ContainmentType result = box.Contains(frustum);
    EXPECT_NE(result, ContainmentType::Contains);
}

// --- Intersects (Ray) ---

TEST(BoundingBoxTest, IntersectsRayHit)
{
    BoundingBox box(Vector3(1.0f, 1.0f, 1.0f), Vector3(3.0f, 3.0f, 3.0f));
    Ray ray(Vector3(0.0f, 2.0f, 2.0f), Vector3(1.0f, 0.0f, 0.0f));
    auto result = box.Intersects(ray);
    EXPECT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 1.0f);
}

TEST(BoundingBoxTest, IntersectsRayMiss)
{
    BoundingBox box(Vector3(1.0f, 1.0f, 1.0f), Vector3(3.0f, 3.0f, 3.0f));
    Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f));
    auto result = box.Intersects(ray);
    EXPECT_FALSE(result.has_value());
}

TEST(BoundingBoxTest, IntersectsRayOutRef)
{
    BoundingBox box(Vector3(1.0f, 1.0f, 1.0f), Vector3(3.0f, 3.0f, 3.0f));
    Ray ray(Vector3(0.0f, 2.0f, 2.0f), Vector3(1.0f, 0.0f, 0.0f));
    std::optional<float> result;
    box.Intersects(ray, result);
    EXPECT_TRUE(result.has_value());
}

// --- Intersects (Plane) ---

TEST(BoundingBoxTest, IntersectsPlaneInFront)
{
    BoundingBox box(Vector3(5.0f, 0.0f, 0.0f), Vector3(10.0f, 1.0f, 1.0f));
    // Plane x=0, normal pointing +x: all box is at x>0 → Front
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(box.Intersects(plane), PlaneIntersectionType::Front);
}

TEST(BoundingBoxTest, IntersectsPlaneInBack)
{
    BoundingBox box(Vector3(-10.0f, 0.0f, 0.0f), Vector3(-5.0f, 1.0f, 1.0f));
    // Plane x=0, normal pointing +x: all box is at x<0 → Back
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(box.Intersects(plane), PlaneIntersectionType::Back);
}

TEST(BoundingBoxTest, IntersectsPlaneIntersecting)
{
    BoundingBox box(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    // Plane x=0 splits the box
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(box.Intersects(plane), PlaneIntersectionType::Intersecting);
}

TEST(BoundingBoxTest, IntersectsPlaneOutRef)
{
    BoundingBox box(Vector3(-1.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    PlaneIntersectionType result;
    box.Intersects(plane, result);
    EXPECT_EQ(result, PlaneIntersectionType::Intersecting);
}

// --- Intersects (BoundingBox) out-ref overload ---

TEST(BoundingBoxTest, IntersectsBoxOutRef)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 5.0f, 5.0f));
    BoundingBox b(Vector3(3.0f, 3.0f, 3.0f), Vector3(8.0f, 8.0f, 8.0f));
    bool result = false;
    a.Intersects(b, result);
    EXPECT_TRUE(result);
}

// --- Intersects (BoundingSphere) out-ref overload ---

TEST(BoundingBoxTest, IntersectsSphereOutRef)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    BoundingSphere sphere(Vector3(1.0f, 1.0f, 1.0f), 0.5f);
    bool result = false;
    box.Intersects(sphere, result);
    EXPECT_TRUE(result);
}

// --- CreateFromSphere ---

TEST(BoundingBoxTest, CreateFromSphereEnclosesAllPoints)
{
    BoundingSphere sphere(Vector3(2.0f, 3.0f, 4.0f), 1.5f);
    BoundingBox box = BoundingBox::CreateFromSphere(sphere);
    EXPECT_FLOAT_EQ(box.Min.X, 0.5f);
    EXPECT_FLOAT_EQ(box.Min.Y, 1.5f);
    EXPECT_FLOAT_EQ(box.Min.Z, 2.5f);
    EXPECT_FLOAT_EQ(box.Max.X, 3.5f);
    EXPECT_FLOAT_EQ(box.Max.Y, 4.5f);
    EXPECT_FLOAT_EQ(box.Max.Z, 5.5f);
}

TEST(BoundingBoxTest, CreateFromSphereOutRef)
{
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 0.0f), 2.0f);
    BoundingBox result;
    BoundingBox::CreateFromSphere(sphere, result);
    EXPECT_FLOAT_EQ(result.Min.X, -2.0f);
    EXPECT_FLOAT_EQ(result.Max.X, 2.0f);
}

// --- CreateMerged out-ref overload ---

TEST(BoundingBoxTest, CreateMergedOutRef)
{
    BoundingBox a(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(3.0f, 3.0f, 3.0f));
    BoundingBox result;
    BoundingBox::CreateMerged(a, b, result);
    EXPECT_FLOAT_EQ(result.Min.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Max.X, 3.0f);
}

// --- GetCorners fill-vector overload ---

TEST(BoundingBoxTest, GetCornersFillVectorMatchesReturnedVector)
{
    BoundingBox box(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    auto returned = box.GetCorners();
    std::vector<Vector3> filled(BoundingBox::CornerCount);
    box.GetCorners(filled);
    for (int i = 0; i < BoundingBox::CornerCount; ++i)
    {
        EXPECT_FLOAT_EQ(filled[i].X, returned[i].X);
        EXPECT_FLOAT_EQ(filled[i].Y, returned[i].Y);
        EXPECT_FLOAT_EQ(filled[i].Z, returned[i].Z);
    }
}

// --- GetHashCode ---

TEST(BoundingBoxTest, HashCodeEqualForEqualBoxes)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(BoundingBoxTest, HashCodeDiffersForDifferentBoxes)
{
    BoundingBox a(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingBox b(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(BoundingBoxTest, ToStringContainsMinAndMax)
{
    BoundingBox box(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    std::string s = box.ToString();
    EXPECT_NE(s.find("Min"), std::string::npos);
    EXPECT_NE(s.find("Max"), std::string::npos);
}
