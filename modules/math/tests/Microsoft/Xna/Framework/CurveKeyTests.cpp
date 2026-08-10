// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

using namespace Microsoft::Xna::Framework;

// --- constructors ---

TEST(CurveKeyTest, ConstructorTwoArgs)
{
    CurveKey k(1.0f, 2.0f);
    EXPECT_FLOAT_EQ(k.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 2.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 0.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), 0.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
}

TEST(CurveKeyTest, ConstructorFourArgs)
{
    CurveKey k(2.0f, 5.0f, 1.0f, -1.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), -1.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
}

TEST(CurveKeyTest, ConstructorFiveArgs)
{
    CurveKey k(3.0f, 7.0f, 1.0f, -1.0f, CurveContinuity::Step);
    EXPECT_FLOAT_EQ(k.getPositionProperty(), 3.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 7.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), -1.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Step);
}

// --- setters ---

TEST(CurveKeyTest, SetValue)
{
    CurveKey k(0.0f, 3.0f);
    k.setValueProperty(10.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 10.0f);
}

TEST(CurveKeyTest, ContinuitySetterRoundTrips)
{
    CurveKey k(0.0f, 0.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
    k.setContinuityProperty(CurveContinuity::Step);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Step);
}

TEST(CurveKeyTest, SetTangentIn)
{
    CurveKey k(0.0f, 0.0f);
    k.setTangentInProperty(7.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 7.0f);
}

TEST(CurveKeyTest, SetTangentOut)
{
    CurveKey k(0.0f, 0.0f);
    k.setTangentOutProperty(8.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), 8.0f);
}

// --- Clone ---

TEST(CurveKeyTest, Clone)
{
    CurveKey k(1.0f, 2.0f, 0.5f, -0.5f);
    CurveKey c = k.Clone();
    EXPECT_FLOAT_EQ(c.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(c.getTangentInProperty(), 0.5f);
    EXPECT_FLOAT_EQ(c.getTangentOutProperty(), -0.5f);
}

TEST(CurveKeyTest, CloneIsIndependent)
{
    CurveKey k(1.0f, 2.0f);
    CurveKey c = k.Clone();
    c.setValueProperty(99.0f);
    EXPECT_NE(k.getValueProperty(), 99.0f);
}

// --- CompareTo ---

TEST(CurveKeyTest, CompareTo)
{
    CurveKey a(1.0f, 0.0f);
    CurveKey b(2.0f, 0.0f);
    EXPECT_LT(a.CompareTo(b), 0);
    EXPECT_GT(b.CompareTo(a), 0);
    EXPECT_EQ(a.CompareTo(a), 0);
}

// --- Equals ---

TEST(CurveKeyTest, EqualsDirectCall)
{
    CurveKey a(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Smooth);
    CurveKey b(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Smooth);
    CurveKey c(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Step);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_FALSE(a.Equals(c));
}

TEST(CurveKeyTest, EqualsFalsePosition)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(2.0f, 2.0f);
    EXPECT_FALSE(a.Equals(b));
}

TEST(CurveKeyTest, EqualsFalseValue)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 3.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- operators ---

TEST(CurveKeyTest, EqualityOperators)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 2.0f);
    CurveKey c(1.0f, 3.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

// --- GetHashCode ---

TEST(CurveKeyTest, GetHashCodeConsistency)
{
    CurveKey a(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
}

TEST(CurveKeyTest, GetHashCodeEqualForEqualKeys)
{
    CurveKey a(1.0f, 2.0f, 0.5f, -0.5f);
    CurveKey b(1.0f, 2.0f, 0.5f, -0.5f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(CurveKeyTest, GetHashCodeDifferentForDifferentKeys)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 3.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}
