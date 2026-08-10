// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

using Microsoft::Xna::Framework::CurveContinuity;

TEST(CurveContinuityTest, SmoothValueIsZero)
{
    EXPECT_EQ(static_cast<int>(CurveContinuity::Smooth), 0);
}

TEST(CurveContinuityTest, StepValueIsOne)
{
    EXPECT_EQ(static_cast<int>(CurveContinuity::Step), 1);
}

TEST(CurveContinuityTest, ValuesAreDistinct)
{
    EXPECT_NE(CurveContinuity::Smooth, CurveContinuity::Step);
}

TEST(CurveContinuityTest, EqualityHolds)
{
    CurveContinuity a = CurveContinuity::Step;
    CurveContinuity b = CurveContinuity::Step;
    EXPECT_EQ(a, b);
}
