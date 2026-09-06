// SPDX-License-Identifier: MS-PL
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <gtest/gtest.h>
#include <cmath>
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

static constexpr float kEps = 1e-5f;

// --- Construction ---

TEST(BoundingSphereTest, DefaultConstructorHasZeroRadius)
{
    BoundingSphere s;
    EXPECT_FLOAT_EQ(s.Radius, 0.0f);
}

TEST(BoundingSphereTest, ConstructorStoresCenterAndRadius)
{
    BoundingSphere s(Vector3(1.0f, 2.0f, 3.0f), 5.0f);
    EXPECT_FLOAT_EQ(s.Center.X, 1.0f);
    EXPECT_FLOAT_EQ(s.Center.Y, 2.0f);
    EXPECT_FLOAT_EQ(s.Center.Z, 3.0f);
    EXPECT_FLOAT_EQ(s.Radius, 5.0f);
}

// --- Contains (point) ---

TEST(BoundingSphereTest, ContainsCenterPoint)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    EXPECT_EQ(s.Contains(Vector3::Zero), ContainmentType::Contains);
}

TEST(BoundingSphereTest, ContainsPointWithinRadius)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    EXPECT_EQ(s.Contains(Vector3(0.5f, 0.0f, 0.0f)), ContainmentType::Contains);
}

TEST(BoundingSphereTest, DisjointPointOutsideRadius)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    EXPECT_EQ(s.Contains(Vector3(2.0f, 0.0f, 0.0f)), ContainmentType::Disjoint);
}

// --- Contains (sphere) ---

TEST(BoundingSphereTest, ContainsFullyInsideSphere)
{
    BoundingSphere outer(Vector3::Zero, 10.0f);
    BoundingSphere inner(Vector3::Zero, 1.0f);
    EXPECT_EQ(outer.Contains(inner), ContainmentType::Contains);
}

TEST(BoundingSphereTest, ContainsOverlappingSpheresIsIntersects)
{
    BoundingSphere a(Vector3::Zero, 2.0f);
    BoundingSphere b(Vector3(3.0f, 0.0f, 0.0f), 2.0f);
    EXPECT_EQ(a.Contains(b), ContainmentType::Intersects);
}

TEST(BoundingSphereTest, ContainsDisjointSpheresIsDisjoint)
{
    BoundingSphere a(Vector3::Zero, 1.0f);
    BoundingSphere b(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_EQ(a.Contains(b), ContainmentType::Disjoint);
}

// --- Intersects (sphere) ---

TEST(BoundingSphereTest, IntersectsOverlappingSpheresIsTrue)
{
    BoundingSphere a(Vector3::Zero, 2.0f);
    BoundingSphere b(Vector3(3.0f, 0.0f, 0.0f), 2.0f);
    EXPECT_TRUE(a.Intersects(b));
}

TEST(BoundingSphereTest, IntersectsDisjointSpheresIsFalse)
{
    BoundingSphere a(Vector3::Zero, 1.0f);
    BoundingSphere b(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_FALSE(a.Intersects(b));
}

TEST(BoundingSphereTest, IntersectsSymmetric)
{
    BoundingSphere a(Vector3::Zero, 2.0f);
    BoundingSphere b(Vector3(3.0f, 0.0f, 0.0f), 2.0f);
    EXPECT_EQ(a.Intersects(b), b.Intersects(a));
}

// --- Intersects (box) ---

TEST(BoundingSphereTest, IntersectsSphereInsideBox)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f));
    BoundingSphere s(Vector3(2.0f, 2.0f, 2.0f), 0.5f);
    EXPECT_TRUE(s.Intersects(box));
}

TEST(BoundingSphereTest, IntersectsSphereFarFromBox)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f));
    BoundingSphere s(Vector3(10.0f, 10.0f, 10.0f), 0.5f);
    EXPECT_FALSE(s.Intersects(box));
}

// --- CreateFromBoundingBox ---

TEST(BoundingSphereTest, CreateFromBoundingBoxEnclosesBox)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    BoundingSphere s = BoundingSphere::CreateFromBoundingBox(box);
    // All 8 corners should be within or on the sphere
    auto corners = box.GetCorners();
    for (const auto& c : corners) {
        float dx = c.X - s.Center.X;
        float dy = c.Y - s.Center.Y;
        float dz = c.Z - s.Center.Z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        EXPECT_LE(dist, s.Radius + kEps);
    }
}

TEST(BoundingSphereTest, CreateFromBoundingBoxCenterIsBoxCenter)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f));
    BoundingSphere s = BoundingSphere::CreateFromBoundingBox(box);
    EXPECT_NEAR(s.Center.X, 2.0f, kEps);
    EXPECT_NEAR(s.Center.Y, 2.0f, kEps);
    EXPECT_NEAR(s.Center.Z, 2.0f, kEps);
}

// --- CreateFromPoints ---

TEST(BoundingSphereTest, CreateFromPointsEnclosesAllPoints)
{
    std::vector<Vector3> pts = {
        Vector3(-2.0f, 0.0f, 0.0f),
        Vector3(2.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f)
    };
    BoundingSphere s = BoundingSphere::CreateFromPoints(pts);
    for (const auto& p : pts) {
        float dx = p.X - s.Center.X;
        float dy = p.Y - s.Center.Y;
        float dz = p.Z - s.Center.Z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        EXPECT_LE(dist, s.Radius + kEps);
    }
}

// --- CreateMerged ---

TEST(BoundingSphereTest, CreateMergedEnclosesBoths)
{
    BoundingSphere a(Vector3(-5.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere b(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere merged = BoundingSphere::CreateMerged(a, b);
    EXPECT_TRUE(merged.Intersects(a));
    EXPECT_TRUE(merged.Intersects(b));
    EXPECT_GE(merged.Radius, 5.0f);
}

// --- Transform ---

TEST(BoundingSphereTest, TransformByIdentityPreservesSphere)
{
    BoundingSphere s(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    BoundingSphere result = s.Transform(Matrix::getIdentityProperty());
    EXPECT_NEAR(result.Center.X, 1.0f, kEps);
    EXPECT_NEAR(result.Center.Y, 2.0f, kEps);
    EXPECT_NEAR(result.Center.Z, 3.0f, kEps);
    EXPECT_NEAR(result.Radius, 4.0f, kEps);
}

TEST(BoundingSphereTest, TransformByTranslationMovesCenter)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    Matrix t = Matrix::CreateTranslation(3.0f, 0.0f, 0.0f);
    BoundingSphere result = s.Transform(t);
    EXPECT_NEAR(result.Center.X, 3.0f, kEps);
    EXPECT_NEAR(result.Center.Y, 0.0f, kEps);
    EXPECT_NEAR(result.Center.Z, 0.0f, kEps);
}

// --- Equality ---

TEST(BoundingSphereTest, EqualSpheresCompareEqual)
{
    BoundingSphere a(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    BoundingSphere b(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(BoundingSphereTest, DifferentSpheresCompareNotEqual)
{
    BoundingSphere a(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    BoundingSphere b(Vector3(1.0f, 2.0f, 3.0f), 5.0f);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Equals() direct call ---

TEST(BoundingSphereTest, EqualsDirectCallSameSphere)
{
    BoundingSphere a(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    BoundingSphere b(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_TRUE(a.Equals(b));
}

TEST(BoundingSphereTest, EqualsDirectCallDifferentCenter)
{
    BoundingSphere a(Vector3(0.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere b(Vector3(1.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- Contains (Vector3) boundary ---

TEST(BoundingSphereTest, ContainsPointOnBoundaryIsIntersects)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    EXPECT_EQ(s.Contains(Vector3(1.0f, 0.0f, 0.0f)), ContainmentType::Intersects);
}

// --- Contains (Vector3) out-ref overload ---

TEST(BoundingSphereTest, ContainsPointOutRef)
{
    BoundingSphere s(Vector3::Zero, 2.0f);
    ContainmentType result;
    s.Contains(Vector3(0.5f, 0.0f, 0.0f), result);
    EXPECT_EQ(result, ContainmentType::Contains);
    s.Contains(Vector3(5.0f, 0.0f, 0.0f), result);
    EXPECT_EQ(result, ContainmentType::Disjoint);
}

// --- Contains (BoundingBox) ---

TEST(BoundingSphereTest, ContainsBoundingBoxFullyInside)
{
    BoundingSphere s(Vector3(5.0f, 5.0f, 5.0f), 10.0f);
    BoundingBox box(Vector3(4.0f, 4.0f, 4.0f), Vector3(6.0f, 6.0f, 6.0f));
    EXPECT_EQ(s.Contains(box), ContainmentType::Contains);
}

TEST(BoundingSphereTest, ContainsBoundingBoxDisjoint)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    BoundingBox box(Vector3(10.0f, 10.0f, 10.0f), Vector3(11.0f, 11.0f, 11.0f));
    EXPECT_EQ(s.Contains(box), ContainmentType::Disjoint);
}

TEST(BoundingSphereTest, ContainsBoundingBoxIntersects)
{
    BoundingSphere s(Vector3::Zero, 2.0f);
    BoundingBox box(Vector3(1.0f, 1.0f, 1.0f), Vector3(3.0f, 3.0f, 3.0f));
    EXPECT_EQ(s.Contains(box), ContainmentType::Intersects);
}

TEST(BoundingSphereTest, ContainsBoundingBoxOutRef)
{
    BoundingSphere s(Vector3(5.0f, 5.0f, 5.0f), 10.0f);
    BoundingBox box(Vector3(4.0f, 4.0f, 4.0f), Vector3(6.0f, 6.0f, 6.0f));
    ContainmentType result;
    s.Contains(box, result);
    EXPECT_EQ(result, ContainmentType::Contains);
}

// --- Contains (BoundingSphere) out-ref overload ---

TEST(BoundingSphereTest, ContainsSphereOutRef)
{
    BoundingSphere outer(Vector3::Zero, 10.0f);
    BoundingSphere inner(Vector3::Zero, 1.0f);
    ContainmentType result;
    outer.Contains(inner, result);
    EXPECT_EQ(result, ContainmentType::Contains);
}

// --- Contains (BoundingFrustum) ---

TEST(BoundingSphereTest, ContainsFrustumReturnsNonContains)
{
    // A tiny sphere that can't contain a full frustum
    BoundingSphere s(Vector3::Zero, 0.1f);
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, -10.0f),
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(0.785398f, 1.0f, 1.0f, 20.0f);
    BoundingFrustum frustum(view * proj);
    ContainmentType result = s.Contains(frustum);
    EXPECT_NE(result, ContainmentType::Contains);
}

// --- Intersects (BoundingBox) out-ref overload ---

TEST(BoundingSphereTest, IntersectsBoxOutRef)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 4.0f, 4.0f));
    BoundingSphere s(Vector3(2.0f, 2.0f, 2.0f), 0.5f);
    bool result = false;
    s.Intersects(box, result);
    EXPECT_TRUE(result);
}

// --- Intersects (BoundingSphere) out-ref overload ---

TEST(BoundingSphereTest, IntersectsSphereOutRef)
{
    BoundingSphere a(Vector3::Zero, 2.0f);
    BoundingSphere b(Vector3(3.0f, 0.0f, 0.0f), 2.0f);
    bool result = false;
    a.Intersects(b, result);
    EXPECT_TRUE(result);
}

// --- Intersects (BoundingFrustum) ---

TEST(BoundingSphereTest, IntersectsFrustumLargeSphere)
{
    // A large sphere centred at the frustum apex should intersect it
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(0.785398f, 1.0f, 1.0f, 20.0f);
    BoundingFrustum frustum(view * proj);
    BoundingSphere s(Vector3(0.0f, 0.0f, -10.0f), 5.0f);
    EXPECT_TRUE(s.Intersects(frustum));
}

// --- Intersects (Ray) ---

TEST(BoundingSphereTest, IntersectsRayHit)
{
    BoundingSphere s(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    auto result = s.Intersects(ray);
    EXPECT_TRUE(result.has_value());
    EXPECT_NEAR(result.value(), 4.0f, kEps);
}

TEST(BoundingSphereTest, IntersectsRayMiss)
{
    BoundingSphere s(Vector3(5.0f, 5.0f, 0.0f), 1.0f);
    Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    auto result = s.Intersects(ray);
    EXPECT_FALSE(result.has_value());
}

TEST(BoundingSphereTest, IntersectsRayOutRef)
{
    BoundingSphere s(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    std::optional<float> result;
    s.Intersects(ray, result);
    EXPECT_TRUE(result.has_value());
}

// --- Intersects (Plane) ---

TEST(BoundingSphereTest, IntersectsPlaneInFront)
{
    BoundingSphere s(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(s.Intersects(plane), PlaneIntersectionType::Front);
}

TEST(BoundingSphereTest, IntersectsPlaneInBack)
{
    BoundingSphere s(Vector3(-5.0f, 0.0f, 0.0f), 1.0f);
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(s.Intersects(plane), PlaneIntersectionType::Back);
}

TEST(BoundingSphereTest, IntersectsPlaneIntersecting)
{
    BoundingSphere s(Vector3(0.0f, 0.0f, 0.0f), 2.0f);
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(s.Intersects(plane), PlaneIntersectionType::Intersecting);
}

TEST(BoundingSphereTest, IntersectsPlaneOutRef)
{
    BoundingSphere s(Vector3(5.0f, 0.0f, 0.0f), 1.0f);
    Plane plane(Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    PlaneIntersectionType result;
    s.Intersects(plane, result);
    EXPECT_EQ(result, PlaneIntersectionType::Front);
}

// --- CreateFromBoundingBox out-ref overload ---

TEST(BoundingSphereTest, CreateFromBoundingBoxOutRef)
{
    BoundingBox box(Vector3(0.0f, 0.0f, 0.0f), Vector3(2.0f, 2.0f, 2.0f));
    BoundingSphere result;
    BoundingSphere::CreateFromBoundingBox(box, result);
    EXPECT_NEAR(result.Center.X, 1.0f, kEps);
    EXPECT_NEAR(result.Center.Y, 1.0f, kEps);
    EXPECT_NEAR(result.Center.Z, 1.0f, kEps);
}

// --- CreateFromFrustum ---

TEST(BoundingSphereTest, CreateFromFrustumEnclosesCorners)
{
    Matrix view = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 1.0f, 0.0f));
    Matrix proj = Matrix::CreatePerspectiveFieldOfView(0.785398f, 1.0f, 1.0f, 10.0f);
    BoundingFrustum frustum(view * proj);
    BoundingSphere s = BoundingSphere::CreateFromFrustum(frustum);
    auto corners = frustum.GetCorners();
    for (const auto& c : corners)
    {
        float dx = c.X - s.Center.X;
        float dy = c.Y - s.Center.Y;
        float dz = c.Z - s.Center.Z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        EXPECT_LE(dist, s.Radius + kEps);
    }
}

// --- CreateMerged out-ref overload ---

TEST(BoundingSphereTest, CreateMergedOutRef)
{
    BoundingSphere a(Vector3(-3.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere b(Vector3(3.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere result;
    BoundingSphere::CreateMerged(a, b, result);
    EXPECT_GE(result.Radius, 3.0f);
}

// --- Transform out-ref overload ---

TEST(BoundingSphereTest, TransformOutRef)
{
    BoundingSphere s(Vector3::Zero, 1.0f);
    Matrix t = Matrix::CreateTranslation(2.0f, 0.0f, 0.0f);
    BoundingSphere result;
    s.Transform(t, result);
    EXPECT_NEAR(result.Center.X, 2.0f, kEps);
    EXPECT_NEAR(result.Radius, 1.0f, kEps);
}

// --- GetHashCode ---

TEST(BoundingSphereTest, HashCodeEqualForEqualSpheres)
{
    BoundingSphere a(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    BoundingSphere b(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(BoundingSphereTest, HashCodeDiffersForDifferentSpheres)
{
    BoundingSphere a(Vector3(0.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere b(Vector3(0.0f, 0.0f, 0.0f), 2.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(BoundingSphereTest, ToStringContainsCenterAndRadius)
{
    BoundingSphere s(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    std::string str = s.ToString();
    EXPECT_NE(str.find("Center"), std::string::npos);
    EXPECT_NE(str.find("Radius"), std::string::npos);
}

// --- CreateFromPoints edge cases ---

TEST(BoundingSphereTest, CreateFromPointsThrowsOnEmptyVector)
{
    std::vector<Vector3> empty;
    EXPECT_THROW(BoundingSphere::CreateFromPoints(empty), std::invalid_argument);
}

// --- CreateMerged containment edge cases ---

TEST(BoundingSphereTest, CreateMergedOriginalContainsAdditional)
{
    // original is large enough to already contain additional
    BoundingSphere original(Vector3::Zero, 10.0f);
    BoundingSphere additional(Vector3(1.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere result = BoundingSphere::CreateMerged(original, additional);
    EXPECT_NEAR(result.Center.X, original.Center.X, kEps);
    EXPECT_NEAR(result.Center.Y, original.Center.Y, kEps);
    EXPECT_NEAR(result.Center.Z, original.Center.Z, kEps);
    EXPECT_NEAR(result.Radius, original.Radius, kEps);
}

TEST(BoundingSphereTest, CreateMergedAdditionalContainsOriginal)
{
    // additional is large enough to already contain original
    BoundingSphere original(Vector3(1.0f, 0.0f, 0.0f), 1.0f);
    BoundingSphere additional(Vector3::Zero, 10.0f);
    BoundingSphere result = BoundingSphere::CreateMerged(original, additional);
    EXPECT_NEAR(result.Center.X, additional.Center.X, kEps);
    EXPECT_NEAR(result.Center.Y, additional.Center.Y, kEps);
    EXPECT_NEAR(result.Center.Z, additional.Center.Z, kEps);
    EXPECT_NEAR(result.Radius, additional.Radius, kEps);
}

// --- CreateFromPoints against the genuine runtime -------------------------------------------
//
// plans/plan_xnapipeline_parity.md XNAPP-266. The sphere is seeded from the widest axis, and when
// two axes tie something has to break it. FNA keeps the earlier axis and CNA followed it; XNA keeps
// the later one, which is why a right triangle's mesh bounding sphere came out mirrored in the
// model differential. Measured directly rather than inferred from a model
// (tests/reference/xna40/framework/framework-packing-oracle.json, cases boundingsphere/*).
namespace
{
    /** @brief The oracle's own wording, so a failure quotes what XNA answered. */
    [[nodiscard]] std::string DescribeSphere(const BoundingSphere& sphere)
    {
        const auto number = [](float value)
        {
            std::ostringstream text;
            text.imbue(std::locale::classic());
            text << std::setprecision(9) << value;
            std::string spelled = text.str();
            return spelled;
        };
        return "center=(" + number(sphere.Center.X) + "," + number(sphere.Center.Y) + "," +
               number(sphere.Center.Z) + ") radius=" + number(sphere.Radius);
    }
}

TEST(BoundingSphereTest, CreateFromPointsBreaksAnAxisTieTheWayXnaDoes)
{
    // The exact centre XNA answered, to the digits it printed.
    const BoundingSphere tie = BoundingSphere::CreateFromPoints(
        {Vector3(0, 0, 0), Vector3(2, 0, 0), Vector3(0, 2, 0)});
    EXPECT_NEAR(tie.Center.X, 0.5527864f, 1e-6f) << DescribeSphere(tie);
    EXPECT_NEAR(tie.Center.Y, 0.7236068f, 1e-6f) << DescribeSphere(tie);
    EXPECT_NEAR(tie.Center.Z, 0.0f, 1e-6f) << DescribeSphere(tie);
    EXPECT_NEAR(tie.Radius, 1.618034f, 1e-6f) << DescribeSphere(tie);

    // The same three points in another order, where both tie-breaks give the same answer: this is
    // the case that would still pass if the rule were wrong, which is why it is not the only one.
    const BoundingSphere reversed = BoundingSphere::CreateFromPoints(
        {Vector3(0, 2, 0), Vector3(2, 0, 0), Vector3(0, 0, 0)});
    EXPECT_NEAR(reversed.Center.X, 1.0f, 1e-6f) << DescribeSphere(reversed);
    EXPECT_NEAR(reversed.Center.Y, 1.0f, 1e-6f) << DescribeSphere(reversed);
    EXPECT_NEAR(reversed.Radius, 1.41421354f, 1e-6f) << DescribeSphere(reversed);

    // Three axes tied at once, which is what makes the rule "the last axis wins" rather than
    // "Y beats X".
    const BoundingSphere all = BoundingSphere::CreateFromPoints(
        {Vector3(0, 0, 0), Vector3(2, 0, 0), Vector3(0, 2, 0), Vector3(0, 0, 2)});
    EXPECT_NEAR(all.Center.X, 0.479899734f, 1e-6f) << DescribeSphere(all);
    EXPECT_NEAR(all.Center.Y, 0.263706475f, 1e-6f) << DescribeSphere(all);
    EXPECT_NEAR(all.Center.Z, 0.628196955f, 1e-6f) << DescribeSphere(all);
    EXPECT_NEAR(all.Radius, 1.90778685f, 1e-6f) << DescribeSphere(all);

    // No tie at all: the widest axis wins outright, and these two are mirror images.
    const BoundingSphere widestX = BoundingSphere::CreateFromPoints(
        {Vector3(0, 0, 0), Vector3(4, 0, 0), Vector3(0, 2, 0)});
    EXPECT_NEAR(widestX.Center.X, 1.70710683f, 1e-6f) << DescribeSphere(widestX);
    EXPECT_NEAR(widestX.Center.Y, 0.2928931f, 1e-6f) << DescribeSphere(widestX);
    EXPECT_NEAR(widestX.Radius, 2.41421366f, 1e-6f) << DescribeSphere(widestX);

    const BoundingSphere widestY = BoundingSphere::CreateFromPoints(
        {Vector3(0, 0, 0), Vector3(2, 0, 0), Vector3(0, 4, 0)});
    EXPECT_NEAR(widestY.Center.X, 0.2928931f, 1e-6f) << DescribeSphere(widestY);
    EXPECT_NEAR(widestY.Center.Y, 1.70710683f, 1e-6f) << DescribeSphere(widestY);
    EXPECT_NEAR(widestY.Radius, 2.41421366f, 1e-6f) << DescribeSphere(widestY);

    const BoundingSphere single = BoundingSphere::CreateFromPoints({Vector3(1, 2, 3)});
    EXPECT_NEAR(single.Center.X, 1.0f, 1e-6f) << DescribeSphere(single);
    EXPECT_NEAR(single.Radius, 0.0f, 1e-6f) << DescribeSphere(single);
}
