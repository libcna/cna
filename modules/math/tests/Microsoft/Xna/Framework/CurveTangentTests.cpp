// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/CurveTangent.hpp"

using Microsoft::Xna::Framework::CurveTangent;

TEST(CurveTangentTest, FlatValueIsZero)
{
    EXPECT_EQ(static_cast<int>(CurveTangent::Flat), 0);
}

TEST(CurveTangentTest, LinearValueIsOne)
{
    EXPECT_EQ(static_cast<int>(CurveTangent::Linear), 1);
}

TEST(CurveTangentTest, SmoothValueIsTwo)
{
    EXPECT_EQ(static_cast<int>(CurveTangent::Smooth), 2);
}

TEST(CurveTangentTest, ValuesAreDistinct)
{
    EXPECT_NE(CurveTangent::Flat,   CurveTangent::Linear);
    EXPECT_NE(CurveTangent::Linear, CurveTangent::Smooth);
    EXPECT_NE(CurveTangent::Flat,   CurveTangent::Smooth);
}

TEST(CurveTangentTest, EqualityHolds)
{
    CurveTangent a = CurveTangent::Smooth;
    CurveTangent b = CurveTangent::Smooth;
    EXPECT_EQ(a, b);
}
