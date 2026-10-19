// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr std::array<std::int32_t, 8> samples = {
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(),
        -2, -1, 0, 1, 3, 7
    };
    const std::array<Rectangle, 7> boxes = {
        Rectangle(0,0,12,12), Rectangle(10,10,5,5), Rectangle(30,40,2,3),
        Rectangle(), Rectangle(-10,-8,7,3), Rectangle(-4,-3,-2,-1), Rectangle(5,5,0,0)
    };
}

TEST(IntegerValueCompatibilityTest, PointHashUsesWrappedCoordinateSum)
{
    for (const auto x : samples) for (const auto y : samples)
    {
        SCOPED_TRACE(testing::Message() << x << ',' << y);
        const std::uint32_t sum = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y);
        EXPECT_EQ(Point(x,y).GetHashCode(), std::bit_cast<std::int32_t>(sum));
    }
}

TEST(IntegerValueCompatibilityTest, RectangleHashUsesWrappedFieldSum)
{
    for (const auto x : samples) for (const auto y : samples)
    for (const auto width : samples) for (const auto height : samples)
    {
        SCOPED_TRACE(testing::Message() << x << ',' << y << ',' << width << ',' << height);
        const std::uint32_t sum = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) +
            static_cast<std::uint32_t>(width) + static_cast<std::uint32_t>(height);
        EXPECT_EQ(Rectangle(x,y,width,height).GetHashCode(), std::bit_cast<std::int32_t>(sum));
    }
}

TEST(IntegerValueCompatibilityTest, RectangleUnionPreservesBothInputAliases)
{
    for (const auto& first : boxes) for (const auto& second : boxes)
    {
        SCOPED_TRACE(first.ToString() + " / " + second.ToString());
        const auto expected = Rectangle::Union(first, second);
        Rectangle separate;
        Rectangle::Union(first, second, separate);
        EXPECT_EQ(separate, expected);
        auto left = first, right = second;
        Rectangle::Union(left, right, left);
        EXPECT_EQ(left, expected);
        EXPECT_EQ(right, second);
        left = first; right = second;
        Rectangle::Union(left, right, right);
        EXPECT_EQ(right, expected);
        EXPECT_EQ(left, first);
    }
    for (const auto& box : boxes)
    {
        auto all = box;
        Rectangle::Union(all, all, all);
        EXPECT_EQ(all, box);
    }
    Rectangle result;
    Rectangle::Union(Rectangle(0,0,12,12), Rectangle(10,10,5,5), result);
    EXPECT_EQ(result, Rectangle(0,0,15,15));
}

TEST(IntegerValueCompatibilityTest, RectangleIntersectAliasControlRemainsUnchanged)
{
    for (const auto& first : boxes) for (const auto& second : boxes)
    {
        const auto expected = Rectangle::Intersect(first, second);
        auto left = first, right = second;
        Rectangle::Intersect(left, right, left);
        EXPECT_EQ(left, expected);
        EXPECT_EQ(right, second);
        left = first; right = second;
        Rectangle::Intersect(left, right, right);
        EXPECT_EQ(right, expected);
        EXPECT_EQ(left, first);
    }
}
