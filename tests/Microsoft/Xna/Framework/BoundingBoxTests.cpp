#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::ContainmentType;
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
