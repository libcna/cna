#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"

using Microsoft::Xna::Framework::Point;
using Microsoft::Xna::Framework::Rectangle;

// --- Construction ---

TEST(RectangleTest, DefaultConstructorIsEmpty)
{
    Rectangle r;
    EXPECT_EQ(r.X, 0);
    EXPECT_EQ(r.Y, 0);
    EXPECT_EQ(r.Width, 0);
    EXPECT_EQ(r.Height, 0);
    EXPECT_TRUE(r.getIsEmptyProperty());
}

TEST(RectangleTest, ParameterizedConstructorStoresFields)
{
    Rectangle r(10, 20, 100, 50);
    EXPECT_EQ(r.X, 10);
    EXPECT_EQ(r.Y, 20);
    EXPECT_EQ(r.Width, 100);
    EXPECT_EQ(r.Height, 50);
}

// --- Edge properties ---

TEST(RectangleTest, EdgeProperties)
{
    Rectangle r(10, 20, 100, 50);
    EXPECT_EQ(r.getLeftProperty(), 10);
    EXPECT_EQ(r.getRightProperty(), 110);
    EXPECT_EQ(r.getTopProperty(), 20);
    EXPECT_EQ(r.getBottomProperty(), 70);
}

TEST(RectangleTest, CenterOfEvenSizedRectangle)
{
    Rectangle r(0, 0, 100, 80);
    Point c = r.getCenterProperty();
    EXPECT_EQ(c.X, 50);
    EXPECT_EQ(c.Y, 40);
}

TEST(RectangleTest, CenterOfOddSizedRectangleTruncates)
{
    Rectangle r(0, 0, 9, 7);
    Point c = r.getCenterProperty();
    EXPECT_EQ(c.X, 4);
    EXPECT_EQ(c.Y, 3);
}

TEST(RectangleTest, NonEmptyRectangleIsNotEmpty)
{
    Rectangle r(1, 1, 1, 1);
    EXPECT_FALSE(r.getIsEmptyProperty());
}

// --- Contains ---

TEST(RectangleTest, ContainsPointInsideReturnsTrue)
{
    Rectangle r(0, 0, 100, 100);
    EXPECT_TRUE(r.Contains(50, 50));
}

TEST(RectangleTest, ContainsPointOnEdgeReturnsFalse)
{
    Rectangle r(0, 0, 100, 100);
    // XNA: Contains is exclusive on right/bottom edge
    EXPECT_FALSE(r.Contains(100, 50));
    EXPECT_FALSE(r.Contains(50, 100));
}

TEST(RectangleTest, ContainsPointOutsideReturnsFalse)
{
    Rectangle r(10, 10, 50, 50);
    EXPECT_FALSE(r.Contains(5, 30));
    EXPECT_FALSE(r.Contains(30, 70));
}

TEST(RectangleTest, ContainsRectangleFullyInside)
{
    Rectangle outer(0, 0, 100, 100);
    Rectangle inner(10, 10, 50, 50);
    EXPECT_TRUE(outer.Contains(inner));
}

TEST(RectangleTest, ContainsRectanglePartiallyOutside)
{
    Rectangle outer(0, 0, 100, 100);
    Rectangle partial(50, 50, 100, 100);
    EXPECT_FALSE(outer.Contains(partial));
}

// --- Intersects ---

TEST(RectangleTest, IntersectsOverlappingRectangles)
{
    Rectangle a(0, 0, 100, 100);
    Rectangle b(50, 50, 100, 100);
    EXPECT_TRUE(a.Intersects(b));
}

TEST(RectangleTest, IntersectsAdjacentRectanglesReturnsFalse)
{
    Rectangle a(0, 0, 100, 100);
    Rectangle b(100, 0, 100, 100);
    EXPECT_FALSE(a.Intersects(b));
}

TEST(RectangleTest, IntersectsDisjointRectanglesReturnsFalse)
{
    Rectangle a(0, 0, 10, 10);
    Rectangle b(20, 20, 10, 10);
    EXPECT_FALSE(a.Intersects(b));
}

// --- Intersect static ---

TEST(RectangleTest, IntersectReturnsOverlapRegion)
{
    Rectangle a(0, 0, 100, 100);
    Rectangle b(50, 50, 100, 100);
    Rectangle result = Rectangle::Intersect(a, b);
    EXPECT_EQ(result.X, 50);
    EXPECT_EQ(result.Y, 50);
    EXPECT_EQ(result.Width, 50);
    EXPECT_EQ(result.Height, 50);
}

TEST(RectangleTest, IntersectOfDisjointReturnsEmpty)
{
    Rectangle a(0, 0, 10, 10);
    Rectangle b(20, 20, 10, 10);
    Rectangle result = Rectangle::Intersect(a, b);
    EXPECT_TRUE(result.getIsEmptyProperty());
}

// --- Union ---

TEST(RectangleTest, UnionEnclosesBoths)
{
    Rectangle a(0, 0, 10, 10);
    Rectangle b(20, 20, 10, 10);
    Rectangle result = Rectangle::Union(a, b);
    EXPECT_EQ(result.X, 0);
    EXPECT_EQ(result.Y, 0);
    EXPECT_EQ(result.Width, 30);
    EXPECT_EQ(result.Height, 30);
}

// --- Equality ---

TEST(RectangleTest, EqualRectanglesCompareEqual)
{
    Rectangle a(1, 2, 3, 4);
    Rectangle b(1, 2, 3, 4);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(RectangleTest, DifferentRectanglesCompareNotEqual)
{
    Rectangle a(1, 2, 3, 4);
    Rectangle b(1, 2, 3, 5);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Offset and Inflate ---

TEST(RectangleTest, OffsetByPoint)
{
    Rectangle r(10, 20, 100, 50);
    r.Offset(Point(5, -10));
    EXPECT_EQ(r.X, 15);
    EXPECT_EQ(r.Y, 10);
    EXPECT_EQ(r.Width, 100);
    EXPECT_EQ(r.Height, 50);
}

TEST(RectangleTest, InflateExpandsEdges)
{
    Rectangle r(10, 20, 100, 50);
    r.Inflate(5, 10);
    EXPECT_EQ(r.X, 5);
    EXPECT_EQ(r.Y, 10);
    EXPECT_EQ(r.Width, 110);
    EXPECT_EQ(r.Height, 70);
}
