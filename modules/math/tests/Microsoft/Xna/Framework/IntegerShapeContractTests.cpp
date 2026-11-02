// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <vector>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework;

TEST(IntegerShapeContractTest, PointEqualityObservesEachCoordinateAndZeroPropertyReturnsACopy)
{
    const Point value(3, -7);
    for (const Point different : {Point(4, -7), Point(3, -6)})
    {
        EXPECT_FALSE(value.Equals(different));
        EXPECT_FALSE(value == different);
        EXPECT_TRUE(value != different);
    }
    auto zero = Point::getZeroProperty();
    EXPECT_EQ(zero.X, 0);
    EXPECT_EQ(zero.Y, 0);
    zero.X = 12;
    EXPECT_EQ(Point::getZeroProperty().X, 0);
    EXPECT_EQ(Point::Zero.X, 0);
    EXPECT_EQ((Point(-7, 7) / Point(3, -3)), Point(-2, -2));
}

TEST(IntegerShapeContractTest, RectangleEqualityObservesEachField)
{
    const Rectangle value(1, 2, 3, 4);
    EXPECT_TRUE(value.Equals(Rectangle(1, 2, 3, 4)));
    for (const Rectangle different : {Rectangle(9, 2, 3, 4), Rectangle(1, 9, 3, 4),
                                      Rectangle(1, 2, 9, 4), Rectangle(1, 2, 3, 9)})
    {
        EXPECT_FALSE(value.Equals(different));
        EXPECT_FALSE(value == different);
        EXPECT_TRUE(value != different);
    }
}

TEST(IntegerShapeContractTest, RectanglePointOverloadsIncludeLeftTopAndExcludeRightBottom)
{
    const Rectangle rectangle(-3, 5, 4, 3);
    struct Case { Point point; bool contained; };
    const Case cases[] = {{Point(-3, 5), true}, {Point(0, 7), true},
                          {Point(1, 5), false}, {Point(-3, 8), false},
                          {Point(-4, 5), false}, {Point(-3, 4), false}};
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.point.ToString());
        EXPECT_EQ(rectangle.Contains(test.point.X, test.point.Y), test.contained);
        EXPECT_EQ(rectangle.Contains(test.point), test.contained);
        bool output = !test.contained;
        rectangle.Contains(test.point, output);
        EXPECT_EQ(output, test.contained);
    }
}

TEST(IntegerShapeContractTest, RectangleContainmentIncludesCoincidentAndEmptyBoundaryRectangles)
{
    const Rectangle outer(-3, 5, 4, 3);
    for (const Rectangle contained : {outer, Rectangle(1, 8, 0, 0), Rectangle(-3, 5, 0, 0)})
    {
        EXPECT_TRUE(outer.Contains(contained));
        bool output = false;
        outer.Contains(contained, output);
        EXPECT_TRUE(output);
    }
    const Rectangle beyond(2, 8, 0, 0);
    EXPECT_FALSE(outer.Contains(beyond));
    bool output = true;
    outer.Contains(beyond, output);
    EXPECT_FALSE(output);
}

TEST(IntegerShapeContractTest, RectangleEmptyMeansAllFourFieldsAreZero)
{
    EXPECT_TRUE(Rectangle::Empty.getIsEmptyProperty());
    EXPECT_EQ(Rectangle::Empty.ToString(), "{X:0 Y:0 Width:0 Height:0}");
    for (const Rectangle value : {Rectangle(1, 0, 0, 0), Rectangle(0, 1, 0, 0),
                                  Rectangle(0, 0, 1, 0), Rectangle(0, 0, 0, 1), Rectangle(0, 0, -1, -1)})
        EXPECT_FALSE(value.getIsEmptyProperty());
}

TEST(IntegerShapeContractTest, NegativeDimensionsAreStoredAndCenterTruncatesTowardZero)
{
    Rectangle value(10, -10, -5, -7);
    EXPECT_EQ(value.getLeftProperty(), 10);
    EXPECT_EQ(value.getTopProperty(), -10);
    EXPECT_EQ(value.getRightProperty(), 5);
    EXPECT_EQ(value.getBottomProperty(), -17);
    EXPECT_EQ(value.getCenterProperty(), Point(8, -13));
    EXPECT_EQ(value.ToString(), "{X:10 Y:-10 Width:-5 Height:-7}");
    value.setLocationProperty(Point(-3, 4));
    EXPECT_EQ(value.getLocationProperty(), Point(-3, 4));
    EXPECT_EQ(value.Width, -5);
    EXPECT_EQ(value.Height, -7);
}

TEST(IntegerShapeContractTest, NegativeInflationCanInvertDimensions)
{
    Rectangle value(10, 20, 4, 6);
    value.Inflate(-3, -4);
    EXPECT_EQ(value, Rectangle(13, 24, -2, -2));
}

TEST(IntegerShapeContractTest, TouchingIntersectionClearsOutValueAndEmptyUnionIncludesOrigin)
{
    const Rectangle first(10, 20, 4, 6), touching(14, 20, 3, 6);
    bool intersects = true;
    first.Intersects(touching, intersects);
    EXPECT_FALSE(intersects);
    Rectangle output(99, 99, 99, 99);
    Rectangle::Intersect(first, touching, output);
    EXPECT_EQ(output, Rectangle(0, 0, 0, 0));
    EXPECT_EQ(Rectangle::Intersect(first, touching), Rectangle(0, 0, 0, 0));
    EXPECT_EQ(Rectangle::Union(first, Rectangle::Empty), Rectangle(0, 0, 14, 26));
    Rectangle::Union(first, Rectangle::Empty, output);
    EXPECT_EQ(output, Rectangle(0, 0, 14, 26));
}

TEST(BoundingBoxCornerContractTest, BothOverloadsHaveLiteralCornerOrderAndFillPreservesTail)
{
    const BoundingBox box(Vector3(-1, -2, -3), Vector3(4, 5, 6));
    const std::vector<Vector3> expected{Vector3(-1, 5, 6), Vector3(4, 5, 6),
        Vector3(4, -2, 6), Vector3(-1, -2, 6), Vector3(-1, 5, -3), Vector3(4, 5, -3),
        Vector3(4, -2, -3), Vector3(-1, -2, -3)};
    EXPECT_EQ(box.GetCorners(), expected);
    const Vector3 guard(99);
    std::vector<Vector3> filled(10, guard);
    box.GetCorners(filled);
    ASSERT_EQ(filled.size(), 10u);
    for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_EQ(filled[i], expected[i]);
    EXPECT_EQ(filled[8], guard);
    EXPECT_EQ(filled[9], guard);
    std::vector<Vector3> shortArray(7, guard);
    EXPECT_THROW(box.GetCorners(shortArray), System::ArgumentOutOfRangeException);
    EXPECT_EQ(shortArray, std::vector<Vector3>(7, guard));
}
