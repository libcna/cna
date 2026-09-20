// SPDX-License-Identifier: MS-PL
//
// XNA's math value types all override `Equals(object)`. In CNA a boxed CLR object is a
// `std::any`, the representation the Content, Design, GamerServices and Net modules already use,
// so the overload is `Equals(const std::any&)`. Every one of the fourteen documented types is
// covered here for the four cases the CLR contract distinguishes: an equal value, an unequal
// value, an object of a different type, and an empty object. The Microsoft implementations are
// xna4-decomp/.../Microsoft.Xna.Framework/{BoundingBox,Vector3,...}.cs.

#include <gtest/gtest.h>

#include <any>
#include <string>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    /// The whole documented `Equals(object)` contract for one type, driven by two unequal values.
    template <typename T>
    void ExpectObjectEquality(const T& value, const T& equalToValue, const T& different)
    {
        ASSERT_TRUE(value.Equals(equalToValue));
        ASSERT_FALSE(value.Equals(different));

        EXPECT_TRUE(value.Equals(std::any(equalToValue)));
        EXPECT_FALSE(value.Equals(std::any(different)));

        // A different boxed type is never equal, whatever it holds.
        EXPECT_FALSE(value.Equals(std::any(7)));
        EXPECT_FALSE(value.Equals(std::any(std::string("value"))));
        EXPECT_FALSE(value.Equals(std::any(3.5f)));

        // An empty object is the C++ counterpart of a null reference: unequal, never a crash.
        EXPECT_FALSE(value.Equals(std::any()));

        // Equal values must stay hash-compatible; that is the half of the CLR contract that binds.
        EXPECT_EQ(value.GetHashCode(), equalToValue.GetHashCode());
    }
}

TEST(ObjectEqualityContractTest, BoundingBox)
{
    ExpectObjectEquality(BoundingBox(Vector3(0.0f), Vector3(1.0f)),
                         BoundingBox(Vector3(0.0f), Vector3(1.0f)),
                         BoundingBox(Vector3(0.0f), Vector3(2.0f)));
}

TEST(ObjectEqualityContractTest, BoundingFrustum)
{
    // BoundingFrustum is a CLR reference type whose Equals(object) compares only the matrix.
    ExpectObjectEquality(BoundingFrustum(Matrix::CreateScale(1.0f)),
                         BoundingFrustum(Matrix::CreateScale(1.0f)),
                         BoundingFrustum(Matrix::CreateScale(2.0f)));
}

TEST(ObjectEqualityContractTest, BoundingSphere)
{
    ExpectObjectEquality(BoundingSphere(Vector3(1.0f, 2.0f, 3.0f), 4.0f),
                         BoundingSphere(Vector3(1.0f, 2.0f, 3.0f), 4.0f),
                         BoundingSphere(Vector3(1.0f, 2.0f, 3.0f), 5.0f));
}

TEST(ObjectEqualityContractTest, Color)
{
    ExpectObjectEquality(Color(10, 20, 30, 40), Color(10, 20, 30, 40), Color(10, 20, 30, 41));
}

TEST(ObjectEqualityContractTest, CurveKey)
{
    ExpectObjectEquality(CurveKey(1.0f, 2.0f), CurveKey(1.0f, 2.0f), CurveKey(1.0f, 3.0f));
}

TEST(ObjectEqualityContractTest, Matrix)
{
    ExpectObjectEquality(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                         Matrix::CreateScale(2.0f));
}

TEST(ObjectEqualityContractTest, Plane)
{
    ExpectObjectEquality(Plane(Vector3(0.0f, 1.0f, 0.0f), 3.0f),
                         Plane(Vector3(0.0f, 1.0f, 0.0f), 3.0f),
                         Plane(Vector3(0.0f, 1.0f, 0.0f), 4.0f));
}

TEST(ObjectEqualityContractTest, Point)
{
    ExpectObjectEquality(Point(3, 4), Point(3, 4), Point(3, 5));
}

TEST(ObjectEqualityContractTest, Quaternion)
{
    ExpectObjectEquality(Quaternion(1.0f, 2.0f, 3.0f, 4.0f), Quaternion(1.0f, 2.0f, 3.0f, 4.0f),
                         Quaternion(1.0f, 2.0f, 3.0f, 5.0f));
}

TEST(ObjectEqualityContractTest, Ray)
{
    ExpectObjectEquality(Ray(Vector3(0.0f), Vector3(0.0f, 0.0f, 1.0f)),
                         Ray(Vector3(0.0f), Vector3(0.0f, 0.0f, 1.0f)),
                         Ray(Vector3(0.0f), Vector3(0.0f, 1.0f, 0.0f)));
}

TEST(ObjectEqualityContractTest, Rectangle)
{
    ExpectObjectEquality(Rectangle(1, 2, 3, 4), Rectangle(1, 2, 3, 4), Rectangle(1, 2, 3, 5));
}

TEST(ObjectEqualityContractTest, Vector2)
{
    ExpectObjectEquality(Vector2(1.0f, 2.0f), Vector2(1.0f, 2.0f), Vector2(1.0f, 3.0f));
}

TEST(ObjectEqualityContractTest, Vector3)
{
    ExpectObjectEquality(Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 2.0f, 3.0f),
                         Vector3(1.0f, 2.0f, 4.0f));
}

TEST(ObjectEqualityContractTest, Vector4)
{
    ExpectObjectEquality(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(1.0f, 2.0f, 3.0f, 4.0f),
                         Vector4(1.0f, 2.0f, 3.0f, 5.0f));
}

TEST(ObjectEqualityContractTest, BoxedEqualityDistinguishesTypesWithTheSameLayout)
{
    // Vector3 and a Vector3-shaped neighbour must not compare equal through a boxed object even
    // when every component agrees, because the CLR type check comes first.
    const Vector3 direction(1.0f, 2.0f, 3.0f);
    const Vector4 wider(1.0f, 2.0f, 3.0f, 0.0f);
    EXPECT_FALSE(direction.Equals(std::any(wider)));
    EXPECT_FALSE(wider.Equals(std::any(direction)));

    const Point point(1, 2);
    const Vector2 vector(1.0f, 2.0f);
    EXPECT_FALSE(point.Equals(std::any(vector)));
    EXPECT_FALSE(vector.Equals(std::any(point)));
}
