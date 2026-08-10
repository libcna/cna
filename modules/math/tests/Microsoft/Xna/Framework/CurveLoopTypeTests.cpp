// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"

using Microsoft::Xna::Framework::CurveLoopType;

TEST(CurveLoopTypeTest, ConstantValueIsZero)
{
    EXPECT_EQ(static_cast<int>(CurveLoopType::Constant), 0);
}

TEST(CurveLoopTypeTest, CycleValueIsOne)
{
    EXPECT_EQ(static_cast<int>(CurveLoopType::Cycle), 1);
}

TEST(CurveLoopTypeTest, CycleOffsetValueIsTwo)
{
    EXPECT_EQ(static_cast<int>(CurveLoopType::CycleOffset), 2);
}

TEST(CurveLoopTypeTest, OscillateValueIsThree)
{
    EXPECT_EQ(static_cast<int>(CurveLoopType::Oscillate), 3);
}

TEST(CurveLoopTypeTest, LinearValueIsFour)
{
    EXPECT_EQ(static_cast<int>(CurveLoopType::Linear), 4);
}

TEST(CurveLoopTypeTest, ValuesAreDistinct)
{
    EXPECT_NE(CurveLoopType::Constant,    CurveLoopType::Cycle);
    EXPECT_NE(CurveLoopType::Cycle,       CurveLoopType::CycleOffset);
    EXPECT_NE(CurveLoopType::CycleOffset, CurveLoopType::Oscillate);
    EXPECT_NE(CurveLoopType::Oscillate,   CurveLoopType::Linear);
}

TEST(CurveLoopTypeTest, EqualityHolds)
{
    CurveLoopType a = CurveLoopType::Oscillate;
    CurveLoopType b = CurveLoopType::Oscillate;
    EXPECT_EQ(a, b);
}
