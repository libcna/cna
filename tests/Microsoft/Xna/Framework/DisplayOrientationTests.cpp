#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"

using Microsoft::Xna::Framework::DisplayOrientation;

TEST(DisplayOrientationTest, DefaultValueIsZero)
{
    EXPECT_EQ(static_cast<int>(DisplayOrientation::Default), 0);
}

TEST(DisplayOrientationTest, LandscapeLeftValueIsOne)
{
    EXPECT_EQ(static_cast<int>(DisplayOrientation::LandscapeLeft), 1);
}

TEST(DisplayOrientationTest, LandscapeRightValueIsTwo)
{
    EXPECT_EQ(static_cast<int>(DisplayOrientation::LandscapeRight), 2);
}

TEST(DisplayOrientationTest, PortraitValueIsFour)
{
    EXPECT_EQ(static_cast<int>(DisplayOrientation::Portrait), 4);
}

TEST(DisplayOrientationTest, ValuesAreDistinct)
{
    EXPECT_NE(DisplayOrientation::Default,       DisplayOrientation::LandscapeLeft);
    EXPECT_NE(DisplayOrientation::LandscapeLeft, DisplayOrientation::LandscapeRight);
    EXPECT_NE(DisplayOrientation::LandscapeLeft, DisplayOrientation::Portrait);
}

TEST(DisplayOrientationTest, BitwiseOrCombinesFlags)
{
    DisplayOrientation combined = DisplayOrientation::LandscapeLeft | DisplayOrientation::Portrait;
    EXPECT_EQ(static_cast<int>(combined), 1 | 4);
}

TEST(DisplayOrientationTest, BitwiseAndIsolatesFlag)
{
    DisplayOrientation combined = DisplayOrientation::LandscapeLeft | DisplayOrientation::Portrait;
    EXPECT_EQ(combined & DisplayOrientation::LandscapeLeft, DisplayOrientation::LandscapeLeft);
    EXPECT_EQ(combined & DisplayOrientation::LandscapeRight, DisplayOrientation::Default);
}

TEST(DisplayOrientationTest, OrAssignAccumulatesFlags)
{
    DisplayOrientation o = DisplayOrientation::LandscapeLeft;
    o |= DisplayOrientation::Portrait;
    EXPECT_EQ(static_cast<int>(o), 1 | 4);
}

TEST(DisplayOrientationTest, AndAssignClearsFlag)
{
    DisplayOrientation o = DisplayOrientation::LandscapeLeft | DisplayOrientation::Portrait;
    o &= DisplayOrientation::LandscapeLeft;
    EXPECT_EQ(o, DisplayOrientation::LandscapeLeft);
}

TEST(DisplayOrientationTest, EqualityHolds)
{
    DisplayOrientation a = DisplayOrientation::Portrait;
    DisplayOrientation b = DisplayOrientation::Portrait;
    EXPECT_EQ(a, b);
}
