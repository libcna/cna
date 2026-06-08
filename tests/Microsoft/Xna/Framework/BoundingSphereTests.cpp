#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::ContainmentType;
using Microsoft::Xna::Framework::Matrix;
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
