#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"

using namespace Microsoft::Xna::Framework;

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

TEST(RayTest, ConstructorSetsFields)
{
    Ray r(Vector3(1.0f, 2.0f, 3.0f), Vector3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(r.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(r.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(r.Position.Z, 3.0f);
    EXPECT_FLOAT_EQ(r.Direction.X, 0.0f);
    EXPECT_FLOAT_EQ(r.Direction.Y, 1.0f);
    EXPECT_FLOAT_EQ(r.Direction.Z, 0.0f);
}

TEST(RayTest, DefaultConstructorIsZero)
{
    Ray r;
    EXPECT_FLOAT_EQ(r.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(r.Direction.Y, 0.0f);
}

// -----------------------------------------------------------------------
// Equality
// -----------------------------------------------------------------------

TEST(RayTest, EqualsReturnsTrueForSameRay)
{
    Ray a(Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f));
    Ray b(Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(RayTest, EqualsReturnsFalseForDifferentPosition)
{
    Ray a(Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f));
    Ray b(Vector3(2.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(RayTest, EqualsReturnsFalseForDifferentDirection)
{
    Ray a(Vector3::Zero, Vector3::UnitX);
    Ray b(Vector3::Zero, Vector3::UnitY);
    EXPECT_FALSE(a.Equals(b));
}

// -----------------------------------------------------------------------
// Intersects(BoundingBox)
// -----------------------------------------------------------------------

TEST(RayTest, IntersectsBoundingBoxHit)
{
    // Ray along +Z toward unit box centred at (0,0,2)
    BoundingBox box(Vector3(-1.0f, -1.0f, 1.0f), Vector3(1.0f, 1.0f, 3.0f));
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    auto result = r.Intersects(box);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 1.0f);
}

TEST(RayTest, IntersectsBoundingBoxMissLeft)
{
    BoundingBox box(Vector3(2.0f, 0.0f, 0.0f), Vector3(4.0f, 2.0f, 2.0f));
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f)); // fires along Z, box is to the right
    auto result = r.Intersects(box);
    EXPECT_FALSE(result.has_value());
}

TEST(RayTest, IntersectsBoundingBoxRayOriginInside)
{
    // Origin is inside the box → distance is 0.0f
    BoundingBox box(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
    Ray r(Vector3::Zero, Vector3(1.0f, 0.0f, 0.0f));
    auto result = r.Intersects(box);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 0.0f);
}

TEST(RayTest, IntersectsBoundingBoxOutRef)
{
    BoundingBox box(Vector3(-1.0f, -1.0f, 4.0f), Vector3(1.0f, 1.0f, 6.0f));
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    std::optional<float> result;
    r.Intersects(box, result);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 4.0f);
}

// -----------------------------------------------------------------------
// Intersects(BoundingSphere)
// -----------------------------------------------------------------------

TEST(RayTest, IntersectsBoundingSphereHit)
{
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 5.0f), 1.0f);
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    auto result = r.Intersects(sphere);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 4.0f, 1e-5f);
}

TEST(RayTest, IntersectsBoundingSphereMiss)
{
    BoundingSphere sphere(Vector3(5.0f, 5.0f, 0.0f), 1.0f);
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    auto result = r.Intersects(sphere);
    EXPECT_FALSE(result.has_value());
}

TEST(RayTest, IntersectsBoundingSphereOriginInside)
{
    BoundingSphere sphere(Vector3::Zero, 2.0f);
    Ray r(Vector3::Zero, Vector3(1.0f, 0.0f, 0.0f));
    auto result = r.Intersects(sphere);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 0.0f);
}

TEST(RayTest, IntersectsBoundingSphereOutRef)
{
    BoundingSphere sphere(Vector3(0.0f, 0.0f, 3.0f), 1.0f);
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    std::optional<float> result;
    r.Intersects(sphere, result);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(*result, 2.0f, 1e-5f);
}

// -----------------------------------------------------------------------
// Intersects(Plane)
// -----------------------------------------------------------------------

TEST(RayTest, IntersectsPlaneHit)
{
    // XY plane at z=5: normal=(0,0,1), d=-5
    Plane plane(Vector3(0.0f, 0.0f, 1.0f), -5.0f);
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    auto result = r.Intersects(plane);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 5.0f);
}

TEST(RayTest, IntersectsPlaneParallelMiss)
{
    // Ray parallel to the XZ plane, not on it
    Plane plane(Vector3(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane
    Ray r(Vector3(0.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f)); // at y=1, goes along X
    auto result = r.Intersects(plane);
    EXPECT_FALSE(result.has_value());
}

TEST(RayTest, IntersectsPlaneOutRef)
{
    Plane plane(Vector3(0.0f, 0.0f, 1.0f), -3.0f);
    Ray r(Vector3::Zero, Vector3(0.0f, 0.0f, 1.0f));
    std::optional<float> result;
    r.Intersects(plane, result);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(*result, 3.0f);
}

// -----------------------------------------------------------------------
// ToString
// -----------------------------------------------------------------------

TEST(RayTest, ToStringContainsPositionAndDirection)
{
    Ray r(Vector3(1.0f, 2.0f, 3.0f), Vector3(0.0f, 1.0f, 0.0f));
    std::string s = r.ToString();
    EXPECT_NE(s.find("Position"), std::string::npos);
    EXPECT_NE(s.find("Direction"), std::string::npos);
}
