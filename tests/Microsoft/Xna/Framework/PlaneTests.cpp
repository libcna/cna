#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"

using namespace Microsoft::Xna::Framework;

static constexpr float kEps = 1e-5f;

// -----------------------------------------------------------------------
// Constructors
// -----------------------------------------------------------------------

TEST(PlaneTest, ConstructorNormalD)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -3.0f);
    EXPECT_FLOAT_EQ(p.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(p.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(p.Normal.Z, 0.0f);
    EXPECT_FLOAT_EQ(p.D, -3.0f);
}

TEST(PlaneTest, ConstructorVector4)
{
    Plane p(Vector4(0.0f, 1.0f, 0.0f, -3.0f));
    EXPECT_FLOAT_EQ(p.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(p.D, -3.0f);
}

TEST(PlaneTest, ConstructorFloats)
{
    Plane p(0.0f, 1.0f, 0.0f, -3.0f);
    EXPECT_FLOAT_EQ(p.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(p.D, -3.0f);
}

TEST(PlaneTest, ConstructorThreePoints)
{
    // XZ plane: points at y=0
    Vector3 a(0.0f, 0.0f, 0.0f);
    Vector3 b(1.0f, 0.0f, 0.0f);
    Vector3 c(0.0f, 0.0f, 1.0f);
    Plane p(a, b, c);
    // Normal should be (0,1,0) or (0,-1,0) depending on winding; D compensates.
    // DotCoordinate for any point on the plane must be ≈ 0
    EXPECT_NEAR(p.DotCoordinate(a), 0.0f, kEps);
    EXPECT_NEAR(p.DotCoordinate(b), 0.0f, kEps);
    EXPECT_NEAR(p.DotCoordinate(c), 0.0f, kEps);
    // Normal must be unit length
    EXPECT_NEAR(p.Normal.Length(), 1.0f, kEps);
}

// -----------------------------------------------------------------------
// Dot, DotCoordinate, DotNormal
// -----------------------------------------------------------------------

TEST(PlaneTest, DotWithVector4)
{
    // Plane y=0 (normal (0,1,0), D=0). Dot(0,1,0,1) = 0*0+1*1+0*0+0*1 = 1.
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    float d = p.Dot(Vector4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(PlaneTest, DotOutRef)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    float result = 0.0f;
    p.Dot(Vector4(0.0f, 1.0f, 0.0f, 1.0f), result);
    EXPECT_NEAR(result, 1.0f, kEps);
}

TEST(PlaneTest, DotCoordinateOnPlaneIsZero)
{
    // y = 2 plane: normal (0,1,0), D = -2
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    EXPECT_NEAR(p.DotCoordinate(Vector3(5.0f, 2.0f, -3.0f)), 0.0f, kEps);
}

TEST(PlaneTest, DotCoordinateAbovePlaneIsPositive)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    EXPECT_GT(p.DotCoordinate(Vector3(0.0f, 3.0f, 0.0f)), 0.0f);
}

TEST(PlaneTest, DotNormal)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -5.0f);
    EXPECT_NEAR(p.DotNormal(Vector3(0.0f, 4.0f, 0.0f)), 4.0f, kEps);
}

TEST(PlaneTest, DotNormalOutRef)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    float result = 0.0f;
    p.DotNormal(Vector3(0.0f, 3.0f, 0.0f), result);
    EXPECT_NEAR(result, 3.0f, kEps);
}

// -----------------------------------------------------------------------
// Normalize
// -----------------------------------------------------------------------

TEST(PlaneTest, NormalizeInPlace)
{
    Plane p(Vector3(0.0f, 2.0f, 0.0f), -4.0f); // not unit normal
    p.Normalize();
    EXPECT_NEAR(p.Normal.Length(), 1.0f, kEps);
    EXPECT_NEAR(p.D, -2.0f, kEps); // D scaled by same factor (1/2)
}

TEST(PlaneTest, NormalizeStatic)
{
    Plane p(Vector3(0.0f, 3.0f, 0.0f), -6.0f);
    Plane n = Plane::Normalize(p);
    EXPECT_NEAR(n.Normal.Length(), 1.0f, kEps);
    EXPECT_NEAR(n.D, -2.0f, kEps);
    // Original untouched
    EXPECT_FLOAT_EQ(p.Normal.Y, 3.0f);
}

TEST(PlaneTest, NormalizeStaticOutRef)
{
    Plane p(Vector3(0.0f, 4.0f, 0.0f), -8.0f);
    Plane result;
    Plane::Normalize(p, result);
    EXPECT_NEAR(result.Normal.Length(), 1.0f, kEps);
    EXPECT_NEAR(result.D, -2.0f, kEps);
}

// -----------------------------------------------------------------------
// Intersects(BoundingBox)
// -----------------------------------------------------------------------

TEST(PlaneTest, IntersectsBoundingBoxFront)
{
    // y=0 plane; box entirely at y>0 → Front
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingBox box(Vector3(0.0f, 1.0f, 0.0f), Vector3(2.0f, 3.0f, 2.0f));
    EXPECT_EQ(p.Intersects(box), PlaneIntersectionType::Front);
}

TEST(PlaneTest, IntersectsBoundingBoxBack)
{
    // y=0 plane; box entirely at y<0 → Back
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingBox box(Vector3(-1.0f, -3.0f, -1.0f), Vector3(1.0f, -1.0f, 1.0f));
    EXPECT_EQ(p.Intersects(box), PlaneIntersectionType::Back);
}

TEST(PlaneTest, IntersectsBoundingBoxIntersecting)
{
    // y=0 plane; box straddles → Intersecting
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingBox box(Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(p.Intersects(box), PlaneIntersectionType::Intersecting);
}

// -----------------------------------------------------------------------
// Intersects(BoundingSphere)
// -----------------------------------------------------------------------

TEST(PlaneTest, IntersectsBoundingSphereFront)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingSphere s(Vector3(0.0f, 3.0f, 0.0f), 1.0f);
    EXPECT_EQ(p.Intersects(s), PlaneIntersectionType::Front);
}

TEST(PlaneTest, IntersectsBoundingSphereBack)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingSphere s(Vector3(0.0f, -3.0f, 0.0f), 1.0f);
    EXPECT_EQ(p.Intersects(s), PlaneIntersectionType::Back);
}

TEST(PlaneTest, IntersectsBoundingSphereIntersecting)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    BoundingSphere s(Vector3(0.0f, 0.5f, 0.0f), 1.0f); // radius straddles plane
    EXPECT_EQ(p.Intersects(s), PlaneIntersectionType::Intersecting);
}

// -----------------------------------------------------------------------
// Equality
// -----------------------------------------------------------------------

TEST(PlaneTest, EqualsSamePlane)
{
    Plane a(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    Plane b(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(PlaneTest, EqualsDifferentD)
{
    Plane a(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    Plane b(Vector3(0.0f, 1.0f, 0.0f), -3.0f);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// -----------------------------------------------------------------------
// Transform
// -----------------------------------------------------------------------

TEST(PlaneTest, TransformByIdentityMatrixUnchanged)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -1.0f);
    Plane q = Plane::Transform(p, Matrix::getIdentityProperty());
    EXPECT_NEAR(q.Normal.Y, 1.0f, kEps);
    EXPECT_NEAR(q.D, -1.0f, kEps);
}

TEST(PlaneTest, TransformByIdentityQuaternionUnchanged)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -1.0f);
    Plane q = Plane::Transform(p, Quaternion::Identity);
    EXPECT_NEAR(q.Normal.Y, 1.0f, kEps);
    EXPECT_NEAR(q.D, -1.0f, kEps);
}

// -----------------------------------------------------------------------
// ToString
// -----------------------------------------------------------------------

TEST(PlaneTest, ToStringContainsNormalAndD)
{
    Plane p(Vector3(0.0f, 1.0f, 0.0f), -2.0f);
    std::string s = p.ToString();
    EXPECT_NE(s.find("Normal"), std::string::npos);
    EXPECT_NE(s.find("D"), std::string::npos);
}
