// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <limits>
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using namespace Microsoft::Xna::Framework;
namespace {
constexpr auto Lo = std::numeric_limits<SharpRuntime::intcs>::min();
constexpr auto Hi = std::numeric_limits<SharpRuntime::intcs>::max();
}

TEST(Tq051IntegerShapeTest, IntersectClearsDegenerateInteriorEvenWhenIntersectsIsTrue)
{
    // Refresh Rectangle.Intersects(value) IL_0000-005c compares cross edges.
    // Intersect(value,value) IL_00a0-00a6 and ref/ref/out IL_0090-0096
    // instead require min(right)>max(left) AND min(bottom)>max(top).
    // For these interior degenerate rectangles Intersects is true, Intersect is Empty.
    const Rectangle outer(0,0,10,10);
    for (const auto& inner : {Rectangle(5,5,0,2), Rectangle(5,5,2,0), Rectangle(5,5,0,0),
                              Rectangle(5,5,-2,2), Rectangle(5,5,2,-2), Rectangle(5,5,-2,-2)})
    {
        SCOPED_TRACE(inner.ToString());
        EXPECT_TRUE(outer.Intersects(inner)); EXPECT_TRUE(inner.Intersects(outer));
        bool result = false; outer.Intersects(inner,result); EXPECT_TRUE(result);
        result = false; inner.Intersects(outer,result); EXPECT_TRUE(result);
        EXPECT_EQ(Rectangle::Intersect(outer,inner), Rectangle(0,0,0,0));
        EXPECT_EQ(Rectangle::Intersect(inner,outer), Rectangle(0,0,0,0));
        Rectangle output(99,98,97,96);
        Rectangle::Intersect(outer,inner,output); EXPECT_EQ(output, Rectangle(0,0,0,0));
        auto left = outer; auto right = inner;
        Rectangle::Intersect(left,right,left);
        EXPECT_EQ(left, Rectangle(0,0,0,0)); EXPECT_EQ(right,inner);
        left = outer; right = inner;
        Rectangle::Intersect(left,right,right);
        EXPECT_EQ(right, Rectangle(0,0,0,0)); EXPECT_EQ(left,outer);
    }
}

TEST(Tq051IntegerShapeTest, ContainsUsesStoredEndpointsForNegativeAndWrappedSizes)
{
    // Refresh Contains(x,y) IL_0016/002f adds unchecked, upper bound exclusive;
    // Contains(Rectangle) IL_001d/002a/004a/0057 adds before signed comparisons.
    // A width of -1 at INT_MIN wraps the right edge to INT_MAX.
    const Rectangle wrapped(Lo,Lo,-1,-1);
    for (const auto& point : {Point(Lo,Lo), Point(0,0), Point(Hi-1,Hi-1)})
    {
        EXPECT_TRUE(wrapped.Contains(point.X,point.Y)); EXPECT_TRUE(wrapped.Contains(point));
        bool output = false; wrapped.Contains(point,output); EXPECT_TRUE(output);
    }
    for (const auto& point : {Point(Hi,0), Point(0,Hi)})
    {
        EXPECT_FALSE(wrapped.Contains(point.X,point.Y)); EXPECT_FALSE(wrapped.Contains(point));
        bool output = true; wrapped.Contains(point,output); EXPECT_FALSE(output);
    }
    const Rectangle outer(0,0,10,10), negative(5,5,-2,-2);
    EXPECT_TRUE(outer.Contains(negative));
    bool output = false; outer.Contains(negative,output); EXPECT_TRUE(output);
    EXPECT_FALSE(negative.Contains(4,4));
    const Rectangle overflowed(Hi-1,Hi-2,4,6);
    EXPECT_FALSE(overflowed.Contains(Hi-1,Hi-2));
    EXPECT_FALSE(overflowed.Contains(Point(Hi-1,Hi-2)));
    output = true; overflowed.Contains(Point(Hi-1,Hi-2),output); EXPECT_FALSE(output);
    EXPECT_TRUE(overflowed.Contains(overflowed));
    output = false; overflowed.Contains(overflowed,output); EXPECT_TRUE(output);
    const Rectangle small(-2,-3,1,1);
    EXPECT_TRUE(wrapped.Contains(small));
    output = false; wrapped.Contains(small,output); EXPECT_TRUE(output);
}

TEST(Tq051IntegerShapeTest, EdgesAndCenterWrapAfterTruncatingHalfSizes)
{
    // Refresh get_Right/get_Bottom IL_000c add (not add.ovf);
    // get_Center IL_000d/001c div truncates, IL_000e/001d add wraps.
    const Rectangle r(Hi-1,Hi-2,4,6);
    EXPECT_EQ(r.getLeftProperty(),Hi-1); EXPECT_EQ(r.getTopProperty(),Hi-2);
    EXPECT_EQ(r.getRightProperty(),Lo+2); EXPECT_EQ(r.getBottomProperty(),Lo+3);
    EXPECT_EQ(r.getCenterProperty(),Point(Lo,Lo));
    EXPECT_EQ(Rectangle(Lo,Lo,-3,-5).getCenterProperty(),Point(Hi,Hi-1));
    EXPECT_EQ(r.getLocationProperty(),Point(Hi-1,Hi-2));
    auto moved = r; moved.setLocationProperty(Point(Lo,Hi));
    EXPECT_EQ(moved,Rectangle(Lo,Hi,4,6));
}

TEST(Tq051IntegerShapeTest, OffsetAndInflateWrapEveryArithmeticStep)
{
    // Refresh Offset(Point) IL_000e/0022 and Offset(int,int) IL_0008/0016 add;
    // Inflate IL_0008/0016 sub, IL_0025/0035 mul, IL_0026/0036 add, all unchecked.
    auto byPoint = Rectangle(Hi,Lo,7,-9); auto byInts = byPoint;
    byPoint.Offset(Point(1,-1)); byInts.Offset(1,-1);
    EXPECT_EQ(byPoint,Rectangle(Lo,Hi,7,-9)); EXPECT_EQ(byInts,Rectangle(Lo,Hi,7,-9));
    Rectangle inflated(Lo,Hi,1,-1); inflated.Inflate(1,-1);
    EXPECT_EQ(inflated,Rectangle(Hi,Lo,3,-3));
    inflated = Rectangle(1,2,3,4); inflated.Inflate(1073741824,Lo);
    EXPECT_EQ(inflated,Rectangle(-1073741823,Lo+2,Lo+3,4));
}

TEST(Tq051IntegerShapeTest, UnionAndIntersectWrapExtentsAndPreserveAliases)
{
    // Refresh Union(value,value) endpoints IL_000e-0040, extents sub IL_00b1/00bc;
    // Intersect(value,value) sub IL_00bc/00c6 (out form IL_00a9/00b2).
    // MAX - MIN = 2^32-1 -> -1; the signed endpoint interval is still valid.
    const Rectangle first(Lo,Lo,1,1), second(Hi-1,Hi-1,1,1), expected(Lo,Lo,-1,-1);
    EXPECT_EQ(Rectangle::Union(first,second),expected);
    Rectangle output(99,98,97,96); Rectangle::Union(first,second,output); EXPECT_EQ(output,expected);
    auto left=first; auto right=second; Rectangle::Union(left,right,left);
    EXPECT_EQ(left,expected); EXPECT_EQ(right,second);
    left=first; right=second; Rectangle::Union(left,right,right);
    EXPECT_EQ(right,expected); EXPECT_EQ(left,first);
    EXPECT_TRUE(expected.Intersects(expected));
    bool intersects = false; expected.Intersects(expected,intersects); EXPECT_TRUE(intersects);
    EXPECT_EQ(Rectangle::Intersect(expected,expected),expected);
    auto all=expected; Rectangle::Intersect(all,all,all); EXPECT_EQ(all,expected);
    // A wrapped positive-size edge falls below its left edge; intersection must clear.
    const Rectangle invalid(Hi-1,0,4,2), control(Hi-1,0,1,2);
    EXPECT_FALSE(invalid.Intersects(control));
    intersects=true; invalid.Intersects(control,intersects); EXPECT_FALSE(intersects);
    EXPECT_EQ(Rectangle::Intersect(invalid,control),Rectangle(0,0,0,0));
    Rectangle::Intersect(invalid,control,output); EXPECT_EQ(output,Rectangle(0,0,0,0));
}

TEST(Tq051IntegerShapeTest, PointArithmeticUsesIndependentSignedCoordinates)
{
    // These existing component-wise operators are CNA extensions: Refresh Point has
    // only == and !=. Hand arithmetic, within int32 range; division truncates toward 0.
    const Point a(-7,11), b(3,-4);
    EXPECT_EQ(a+b,Point(-4,7)); EXPECT_EQ(a-b,Point(-10,15));
    EXPECT_EQ(a*b,Point(-21,-44)); EXPECT_EQ(a/b,Point(-2,-2));
    // Refresh Point.ToString IL_0007 format; full int32 signed decimal extremes.
    EXPECT_EQ(Point(Lo,Hi).ToString(),"{X:-2147483648 Y:2147483647}");
}
