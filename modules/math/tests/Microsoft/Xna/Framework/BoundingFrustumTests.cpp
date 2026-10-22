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
#include "System/ArgumentOutOfRangeException.hpp"

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
    EXPECT_THROW(f.GetCorners(tooSmall), System::ArgumentOutOfRangeException);
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

TEST(BoundingFrustumTest, OrthographicPointContainmentMatchesXnaFarPlaneTolerance)
{
    // cna-engine UP-004 / cna-extended EXT-002: the 2D camera's far plane is Z=0.
    // XNA Contains(ref Vector3, out ContainmentType) checks every plane with 1e-5f
    // tolerance and never returns Intersects for a point.
    const Matrix view = Matrix::CreateLookAt(Vector3::Zero, Vector3(0, 0, -1), Vector3(0, 1, 0));
    const BoundingFrustum frustum(view * Matrix::CreateOrthographic(640.0f, 480.0f, -1.0f, 0.0f));
    const Plane far = frustum.getFarProperty();
    ASSERT_EQ(far.Normal, Vector3(0, 0, -1));
    ASSERT_FLOAT_EQ(far.D, 0.0f);

    const struct
    {
        const char* name;
        Vector3 point;
        ContainmentType expected;
    } cases[] = {
        {"exactly on far plane", Vector3(0, 0, 0), ContainmentType::Contains},
        {"1e-6 beyond far plane", Vector3(0, 0, -1e-6f), ContainmentType::Contains},
        {"1e-3 beyond far plane", Vector3(0, 0, -1e-3f), ContainmentType::Disjoint},
        {"on far plane but outside right plane", Vector3(321, 0, 0), ContainmentType::Disjoint},
    };
    for (const auto& item : cases)
    {
        SCOPED_TRACE(item.name);
        EXPECT_EQ(frustum.Contains(item.point), item.expected);
        ContainmentType output = ContainmentType::Intersects;
        frustum.Contains(item.point, output);
        EXPECT_EQ(output, item.expected);
    }
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
    // Genuine XNA tests geometric containment, including boundary corners,
    // rather than limiting Contains to the pointer-identical instance.
    EXPECT_EQ(a.Contains(b), ContainmentType::Contains);
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

TEST(BoundingFrustumTest, IntersectsRayClipsEveryPlaneWithXnaBoundaryRules)
{
    // The same 22 cases pass against the original XNA 4.0 assembly in
    // cna-demos/docs/repair-evidence/math/FrustumRayOracle.cs.
    struct Case
    {
        const char* name;
        Vector3 origin;
        Vector3 direction;
        std::optional<float> expected;
    };
    const Case cases[] = {
        {"near entry", {0, 0, -3}, Vector3::UnitZ, 3},
        {"far entry", {0, 0, 4}, -Vector3::UnitZ, 3},
        {"left entry", {-4, 0, .5f}, Vector3::UnitX, 3},
        {"right entry", {4, 0, .5f}, -Vector3::UnitX, 3},
        {"bottom entry", {0, -4, .5f}, Vector3::UnitY, 3},
        {"top entry", {0, 4, .5f}, -Vector3::UnitY, 3},
        {"nonunit direction", {0, 0, -3}, {0, 0, 2}, 1.5f},
        {"behind ray", {0, 0, -3}, -Vector3::UnitZ, std::nullopt},
        {"parallel outside", {3, 0, -3}, Vector3::UnitZ, std::nullopt},
        {"disjoint intervals", {3, 0, -3}, {-1, 0, 3}, std::nullopt},
        {"edge tangent", {1, 1, -3}, Vector3::UnitZ, 3},
        {"corner contact", {2, 0, -1}, {-1, 1, 1}, 1},
        {"boundary inward", Vector3::Zero, Vector3::UnitZ, 0},
        {"boundary outward", Vector3::Zero, -Vector3::UnitZ, 0},
        {"boundary stationary", Vector3::Zero, Vector3::Zero, 0},
        {"inside stationary", {0, 0, .5f}, Vector3::Zero, 0},
        {"outside stationary", {0, 0, -3}, Vector3::Zero, std::nullopt},
        {"containment tolerance", {0, 0, -1e-6f}, -Vector3::UnitZ, 0},
        {"beyond containment tolerance", {0, 0, -1e-3f}, Vector3::UnitZ, 1e-3f},
        {"direction below parallel epsilon", {0, 0, -1}, {0, 0, 5e-6f}, std::nullopt},
        {"direction at parallel epsilon", {0, 0, -1}, {0, 0, 1e-5f}, 100000},
        {"parallel offset beyond plane but inside tolerance", {1 + 1e-6f, 0, -3},
            Vector3::UnitZ, std::nullopt},
    };
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.name);
        const Ray ray(test.origin, test.direction);
        std::optional<float> output = 99.0f;
        frustum.Intersects(ray, output);
        EXPECT_EQ(output, test.expected);
        EXPECT_EQ(frustum.Intersects(ray), test.expected);
        EXPECT_EQ(ray.Intersects(frustum), test.expected);
    }
}

TEST(BoundingFrustumTest, IntersectsRayUsesTransformedAndCameraFrustumPlanes)
{
    const BoundingFrustum translated(Matrix::CreateTranslation(-10, 20, -30));
    const BoundingFrustum camera(Matrix::CreateLookAt(Vector3(0, 0, 5), Vector3::Zero,
        Vector3::Up) * Matrix::CreateOrthographic(2, 2, 1, 5));
    const Ray translatedRay(Vector3(10, -20, 27), Vector3::UnitZ);
    const Ray cameraRay(Vector3(0, 0, 7), -Vector3::UnitZ);
    std::optional<float> output = 99.0f;
    translated.Intersects(translatedRay, output);
    EXPECT_EQ(output, 3.0f);
    EXPECT_EQ(translated.Intersects(translatedRay), 3.0f);
    EXPECT_EQ(translatedRay.Intersects(translated), 3.0f);
    output = 99.0f;
    camera.Intersects(cameraRay, output);
    EXPECT_EQ(output, 3.0f);
    EXPECT_EQ(camera.Intersects(cameraRay), 3.0f);
    EXPECT_EQ(cameraRay.Intersects(camera), 3.0f);
}

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
