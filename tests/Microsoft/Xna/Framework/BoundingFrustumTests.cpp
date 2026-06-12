// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <optional>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingFrustum;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::ContainmentType;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::PlaneIntersectionType;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Vector3;

static constexpr float kEps = 1e-4f;

// Camera at origin, looking down -Z, near=1, far=100, 90-degree FOV, 1:1 aspect.
// Inside region: x and y within ±depth, z between -1 and -100.
static BoundingFrustum MakeTestFrustum()
{
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(
        MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
    return BoundingFrustum(view * proj);
}

// --- Constructor and Matrix property ---

TEST(BoundingFrustumTest, ConstructorStoresMatrix)
{
    BoundingFrustum f = MakeTestFrustum();
    Matrix m = f.getMatrixProperty();
    // Matrix should be non-identity (has view+projection applied)
    EXPECT_FALSE(m == Matrix::getIdentityProperty());
}

TEST(BoundingFrustumTest, SetMatrixPropertyRecalculatesPlanes)
{
    BoundingFrustum f = MakeTestFrustum();
    Matrix original = f.getMatrixProperty();

    // Create a frustum pointing in a different direction
    Matrix view2 = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj2 = Matrix::CreatePerspectiveFieldOfView(
        MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
    Matrix newMatrix = view2 * proj2;

    f.setMatrixProperty(newMatrix);
    EXPECT_FALSE(f.getMatrixProperty() == original);

    // A point in the original frustum should now be outside
    ContainmentType result = f.Contains(Vector3(0.0f, 0.0f, -50.0f));
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- CornerCount constant ---

TEST(BoundingFrustumTest, CornerCountIsEight)
{
    EXPECT_EQ(BoundingFrustum::CornerCount, 8);
}

// --- Plane accessors ---

TEST(BoundingFrustumTest, PlaneAccessorsReturnValidPlanes)
{
    BoundingFrustum f = MakeTestFrustum();
    // Planes must have unit-length normals (they are normalized)
    auto checkNormalized = [](const Plane& p) {
        float len = p.Normal.Length();
        EXPECT_NEAR(len, 1.0f, kEps);
    };
    checkNormalized(f.getNearProperty());
    checkNormalized(f.getFarProperty());
    checkNormalized(f.getLeftProperty());
    checkNormalized(f.getRightProperty());
    checkNormalized(f.getTopProperty());
    checkNormalized(f.getBottomProperty());
}

// --- GetCorners ---

TEST(BoundingFrustumTest, GetCornersReturnsEightPoints)
{
    BoundingFrustum f = MakeTestFrustum();
    auto corners = f.GetCorners();
    EXPECT_EQ(static_cast<int>(corners.size()), BoundingFrustum::CornerCount);
}

TEST(BoundingFrustumTest, GetCornersFillVectorMatchesReturnedVector)
{
    BoundingFrustum f = MakeTestFrustum();
    auto returned = f.GetCorners();
    std::vector<Vector3> filled(BoundingFrustum::CornerCount);
    f.GetCorners(filled);
    for (int i = 0; i < BoundingFrustum::CornerCount; ++i)
    {
        EXPECT_NEAR(filled[i].X, returned[i].X, kEps);
        EXPECT_NEAR(filled[i].Y, returned[i].Y, kEps);
        EXPECT_NEAR(filled[i].Z, returned[i].Z, kEps);
    }
}

TEST(BoundingFrustumTest, GetCornersTooSmallVectorThrows)
{
    BoundingFrustum f = MakeTestFrustum();
    std::vector<Vector3> tooSmall(BoundingFrustum::CornerCount - 1);
    EXPECT_THROW(f.GetCorners(tooSmall), std::out_of_range);
}

// --- Contains (Vector3) ---

TEST(BoundingFrustumTest, ContainsPointInsideFrustum)
{
    BoundingFrustum f = MakeTestFrustum();
    EXPECT_EQ(f.Contains(Vector3(0.0f, 0.0f, -50.0f)), ContainmentType::Contains);
}

TEST(BoundingFrustumTest, ContainsPointBehindCamera)
{
    BoundingFrustum f = MakeTestFrustum();
    EXPECT_EQ(f.Contains(Vector3(0.0f, 0.0f, 50.0f)), ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsPointBeyondFarPlane)
{
    BoundingFrustum f = MakeTestFrustum();
    EXPECT_EQ(f.Contains(Vector3(0.0f, 0.0f, -200.0f)), ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsPointOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    ContainmentType result;
    f.Contains(Vector3(0.0f, 0.0f, -50.0f), result);
    EXPECT_EQ(result, ContainmentType::Contains);
    f.Contains(Vector3(0.0f, 0.0f, 50.0f), result);
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- Contains (BoundingBox) ---

TEST(BoundingFrustumTest, ContainsBoxInsideFrustum)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-0.5f, -0.5f, -51.0f), Vector3(0.5f, 0.5f, -49.0f));
    ContainmentType result = f.Contains(box);
    EXPECT_NE(result, ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsBoxDisjoint)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-1.0f, -1.0f, 10.0f), Vector3(1.0f, 1.0f, 20.0f));
    EXPECT_EQ(f.Contains(box), ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsBoxOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-1.0f, -1.0f, 10.0f), Vector3(1.0f, 1.0f, 20.0f));
    ContainmentType result;
    f.Contains(box, result);
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- Contains (BoundingSphere) ---

TEST(BoundingFrustumTest, ContainsSphereInsideFrustum)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, -50.0f), 0.5f);
    ContainmentType result = f.Contains(sphere);
    EXPECT_NE(result, ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsSphereDisjoint)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 500.0f), 1.0f);
    EXPECT_EQ(f.Contains(sphere), ContainmentType::Disjoint);
}

TEST(BoundingFrustumTest, ContainsSphereOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 500.0f), 1.0f);
    ContainmentType result;
    f.Contains(sphere, result);
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- Contains (BoundingFrustum) ---

TEST(BoundingFrustumTest, ContainsSelfReturnContains)
{
    BoundingFrustum f = MakeTestFrustum();
    EXPECT_EQ(f.Contains(f), ContainmentType::Contains);
}

TEST(BoundingFrustumTest, ContainsSameFrustumReturnContains)
{
    BoundingFrustum a = MakeTestFrustum();
    BoundingFrustum b = MakeTestFrustum();
    // Different objects, same matrix: corners of b lie exactly on the boundary planes of a
    // (distance = 0 → Intersecting), so FNA-compatible result is Intersects, not Contains.
    // Contains is only returned for the self-reference case (this == &frustum).
    EXPECT_EQ(a.Contains(b), ContainmentType::Intersects);
}

TEST(BoundingFrustumTest, ContainsDifferentFrustumDiverges)
{
    BoundingFrustum a = MakeTestFrustum();
    // Frustum pointing in opposite direction
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, 1.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(
        MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
    BoundingFrustum b(view * proj);
    ContainmentType result = a.Contains(b);
    EXPECT_NE(result, ContainmentType::Contains);
}

// --- Intersects (BoundingFrustum) ---

TEST(BoundingFrustumTest, IntersectsSelfIsTrue)
{
    BoundingFrustum f = MakeTestFrustum();
    EXPECT_TRUE(f.Intersects(f));
}

TEST(BoundingFrustumTest, IntersectsSameFrustumIsTrue)
{
    BoundingFrustum a = MakeTestFrustum();
    BoundingFrustum b = MakeTestFrustum();
    EXPECT_TRUE(a.Intersects(b));
}

// --- Intersects (BoundingBox) ---

TEST(BoundingFrustumTest, IntersectsBoxInsideIsTrue)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-0.5f, -0.5f, -51.0f), Vector3(0.5f, 0.5f, -49.0f));
    EXPECT_TRUE(f.Intersects(box));
}

TEST(BoundingFrustumTest, IntersectsBoxOutsideIsFalse)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-1.0f, -1.0f, 10.0f), Vector3(1.0f, 1.0f, 20.0f));
    EXPECT_FALSE(f.Intersects(box));
}

TEST(BoundingFrustumTest, IntersectsBoxOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingBox box(Vector3(-1.0f, -1.0f, 10.0f), Vector3(1.0f, 1.0f, 20.0f));
    bool result = true;
    f.Intersects(box, result);
    EXPECT_FALSE(result);
}

// --- Intersects (BoundingSphere) ---

TEST(BoundingFrustumTest, IntersectsSphereInsideIsTrue)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, -50.0f), 0.5f);
    EXPECT_TRUE(f.Intersects(sphere));
}

TEST(BoundingFrustumTest, IntersectsSphereOutsideIsFalse)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 500.0f), 1.0f);
    EXPECT_FALSE(f.Intersects(sphere));
}

TEST(BoundingFrustumTest, IntersectsSphereOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 500.0f), 1.0f);
    bool result = true;
    f.Intersects(sphere, result);
    EXPECT_FALSE(result);
}

// --- Intersects (Plane) ---

TEST(BoundingFrustumTest, IntersectsPlaneAllCornersOnOneSide)
{
    BoundingFrustum f = MakeTestFrustum();
    // Plane at z=1000, facing -Z: all frustum corners are at z < 0, so behind the plane
    Plane plane(Vector3(0.0f, 0.0f, -1.0f), -1000.0f);
    PlaneIntersectionType result = f.Intersects(plane);
    // All corners on the same side → Front or Back (not Intersecting)
    EXPECT_NE(result, PlaneIntersectionType::Intersecting);
}

TEST(BoundingFrustumTest, IntersectsPlaneSlicingThroughFrustum)
{
    BoundingFrustum f = MakeTestFrustum();
    // Plane at z=-50, normal +Z: some corners in front, some behind → Intersecting
    Plane plane(Vector3(0.0f, 0.0f, 1.0f), 50.0f);
    EXPECT_EQ(f.Intersects(plane), PlaneIntersectionType::Intersecting);
}

TEST(BoundingFrustumTest, IntersectsPlaneOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    Plane plane(Vector3(0.0f, 0.0f, 1.0f), 50.0f);
    PlaneIntersectionType result;
    f.Intersects(plane, result);
    EXPECT_EQ(result, PlaneIntersectionType::Intersecting);
}

// --- Intersects (Ray) ---

TEST(BoundingFrustumTest, IntersectsRayOriginInsideReturnZero)
{
    BoundingFrustum f = MakeTestFrustum();
    Ray ray(Vector3(0.0f, 0.0f, -50.0f), Vector3(1.0f, 0.0f, 0.0f));
    auto result = f.Intersects(ray);
    EXPECT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 0.0f);
}

TEST(BoundingFrustumTest, IntersectsRayOriginOutsideReturnsNullopt)
{
    BoundingFrustum f = MakeTestFrustum();
    // Origin behind the camera, pointing further away
    Ray ray(Vector3(0.0f, 0.0f, 500.0f), Vector3(0.0f, 0.0f, 1.0f));
    auto result = f.Intersects(ray);
    EXPECT_FALSE(result.has_value());
}

TEST(BoundingFrustumTest, IntersectsRayOutRef)
{
    BoundingFrustum f = MakeTestFrustum();
    Ray ray(Vector3(0.0f, 0.0f, -50.0f), Vector3(1.0f, 0.0f, 0.0f));
    std::optional<float> result;
    f.Intersects(ray, result);
    EXPECT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 0.0f);
}

// --- Equals ---

TEST(BoundingFrustumTest, EqualsDirectCallSameFrustum)
{
    BoundingFrustum a = MakeTestFrustum();
    BoundingFrustum b = MakeTestFrustum();
    EXPECT_TRUE(a.Equals(b));
}

TEST(BoundingFrustumTest, EqualsDirectCallDifferentFrustum)
{
    BoundingFrustum a = MakeTestFrustum();
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(
        MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
    BoundingFrustum b(view * proj);
    EXPECT_FALSE(a.Equals(b));
}

// --- operator== / operator!= ---

TEST(BoundingFrustumTest, EqualFrustumsCompareEqual)
{
    BoundingFrustum a = MakeTestFrustum();
    BoundingFrustum b = MakeTestFrustum();
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(BoundingFrustumTest, DifferentFrustumsCompareNotEqual)
{
    BoundingFrustum a = MakeTestFrustum();
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(
        MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
    BoundingFrustum b(view * proj);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- GetHashCode ---

TEST(BoundingFrustumTest, HashCodeEqualForEqualFrustums)
{
    BoundingFrustum a = MakeTestFrustum();
    BoundingFrustum b = MakeTestFrustum();
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}


// --- ToString ---

TEST(BoundingFrustumTest, ToStringContainsPlaneLabels)
{
    BoundingFrustum f = MakeTestFrustum();
    std::string s = f.ToString();
    EXPECT_NE(s.find("Near"), std::string::npos);
    EXPECT_NE(s.find("Far"), std::string::npos);
    EXPECT_NE(s.find("Left"), std::string::npos);
    EXPECT_NE(s.find("Right"), std::string::npos);
    EXPECT_NE(s.find("Top"), std::string::npos);
    EXPECT_NE(s.find("Bottom"), std::string::npos);
}
