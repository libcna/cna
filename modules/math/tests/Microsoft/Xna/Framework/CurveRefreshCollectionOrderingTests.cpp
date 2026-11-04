// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Curve.hpp"

using namespace Microsoft::Xna::Framework;

TEST(CurveRefreshCollectionOrderingTest, UnorderedNaNPositionsFollowBinarySearchComparison)
{
    // Refresh Add IL_000f-001b uses List.BinarySearch with stored.CompareTo(item).
    // CompareTo IL_0000-0021 returns +1 for either NaN operand, so with [2,4]
    // the insertion index of NaN is zero. A second NaN also goes to index zero.
    const float nan=std::numeric_limits<float>::quiet_NaN();
    CurveKeyCollection keys;
    keys.Add(CurveKey(2,20)); keys.Add(CurveKey(4,40));
    keys.Add(CurveKey(nan,90)); keys.Add(CurveKey(nan,91));
    ASSERT_EQ(keys.getCountProperty(),4);
    EXPECT_TRUE(std::isnan(keys[0].getPositionProperty()));
    EXPECT_TRUE(std::isnan(keys[1].getPositionProperty()));
    EXPECT_EQ(keys[0].getValueProperty(),91);
    EXPECT_EQ(keys[1].getValueProperty(),90);
    EXPECT_EQ(keys[2],CurveKey(2,20));
    EXPECT_EQ(keys[3],CurveKey(4,40));
    // Refresh Clone IL_0006-0012 copies list order without re-running Add.
    const auto copy=keys.Clone();
    ASSERT_EQ(copy.getCountProperty(),4);
    EXPECT_EQ(copy[0].getValueProperty(),91);
    EXPECT_EQ(copy[1].getValueProperty(),90);
    EXPECT_EQ(copy[2],CurveKey(2,20));
    EXPECT_EQ(copy[3],CurveKey(4,40));
}
