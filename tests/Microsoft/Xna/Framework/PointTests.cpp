// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Point.hpp"

using Microsoft::Xna::Framework::Point;

// --- Static constant ---

TEST(PointTest, ZeroHasBothCoordinatesZero)
{
    EXPECT_EQ(Point::Zero.X, 0);
    EXPECT_EQ(Point::Zero.Y, 0);
}

// --- Construction ---

TEST(PointTest, DefaultConstructorIsZero)
{
    Point p;
    EXPECT_EQ(p.X, 0);
    EXPECT_EQ(p.Y, 0);
}

TEST(PointTest, TwoArgumentConstructor)
{
    Point p(3, 7);
    EXPECT_EQ(p.X, 3);
    EXPECT_EQ(p.Y, 7);
}

TEST(PointTest, NegativeCoordinates)
{
    Point p(-5, -10);
    EXPECT_EQ(p.X, -5);
    EXPECT_EQ(p.Y, -10);
}

// --- Equality ---

TEST(PointTest, EqualPointsCompareEqual)
{
    Point a(4, 8);
    Point b(4, 8);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(PointTest, DifferentPointsCompareNotEqual)
{
    Point a(4, 8);
    Point b(4, 9);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(PointTest, EqualsMethodMatchesOperator)
{
    Point a(1, 2);
    Point b(1, 2);
    Point c(1, 3);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_FALSE(a.Equals(c));
}

// --- Arithmetic operators ---

TEST(PointTest, AdditionOperator)
{
    Point result = Point(3, 4) + Point(1, 2);
    EXPECT_EQ(result.X, 4);
    EXPECT_EQ(result.Y, 6);
}

TEST(PointTest, SubtractionOperator)
{
    Point result = Point(5, 7) - Point(2, 3);
    EXPECT_EQ(result.X, 3);
    EXPECT_EQ(result.Y, 4);
}

TEST(PointTest, MultiplicationOperator)
{
    Point result = Point(3, 4) * Point(2, 3);
    EXPECT_EQ(result.X, 6);
    EXPECT_EQ(result.Y, 12);
}

TEST(PointTest, DivisionOperator)
{
    Point result = Point(6, 12) / Point(2, 3);
    EXPECT_EQ(result.X, 3);
    EXPECT_EQ(result.Y, 4);
}

TEST(PointTest, AddingZeroPreservesPoint)
{
    Point p(5, 10);
    EXPECT_EQ((p + Point::Zero).X, 5);
    EXPECT_EQ((p + Point::Zero).Y, 10);
}

TEST(PointTest, SubtractingPointFromItselfIsZero)
{
    Point p(7, 13);
    Point result = p - p;
    EXPECT_EQ(result.X, 0);
    EXPECT_EQ(result.Y, 0);
}

// --- Negative coordinates arithmetic ---

TEST(PointTest, AdditionWithNegativeCoordinates)
{
    Point result = Point(5, 3) + Point(-2, -1);
    EXPECT_EQ(result.X, 3);
    EXPECT_EQ(result.Y, 2);
}

// --- GetHashCode ---

TEST(PointTest, GetHashCodeEqualPointsGiveEqualHash)
{
    Point a(7, 13);
    Point b(7, 13);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(PointTest, GetHashCodeDifferentPointsTypicallyDiffer)
{
    // Uses X ^ Y; choose values where XORs differ
    Point a(1, 2);
    Point b(3, 4);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(PointTest, ToStringFormat)
{
    Point p(3, 7);
    EXPECT_EQ(p.ToString(), "{X:3 Y:7}");
}

TEST(PointTest, ToStringZero)
{
    EXPECT_EQ(Point::Zero.ToString(), "{X:0 Y:0}");
}
