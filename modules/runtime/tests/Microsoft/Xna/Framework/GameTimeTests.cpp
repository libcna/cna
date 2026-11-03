// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameTime.hpp"

using namespace Microsoft::Xna::Framework;

TEST(GameTimeTest, DefaultConstructorZerosAll)
{
    GameTime gt;
    EXPECT_EQ(gt.getTotalGameTimeProperty().getTicksProperty(), 0);
    EXPECT_EQ(gt.getElapsedGameTimeProperty().getTicksProperty(), 0);
    EXPECT_FALSE(gt.getIsRunningSlowlyProperty());
}

TEST(GameTimeTest, TwoArgConstructor)
{
    GameTime gt(TimeSpan(1000), TimeSpan(500));
    EXPECT_EQ(gt.getTotalGameTimeProperty().getTicksProperty(), 1000);
    EXPECT_EQ(gt.getElapsedGameTimeProperty().getTicksProperty(), 500);
    EXPECT_FALSE(gt.getIsRunningSlowlyProperty());
}

TEST(GameTimeTest, ThreeArgConstructorSlowlyTrue)
{
    GameTime gt(TimeSpan(2000), TimeSpan(1000), true);
    EXPECT_EQ(gt.getTotalGameTimeProperty().getTicksProperty(), 2000);
    EXPECT_EQ(gt.getElapsedGameTimeProperty().getTicksProperty(), 1000);
    EXPECT_TRUE(gt.getIsRunningSlowlyProperty());
}

TEST(GameTimeTest, ThreeArgConstructorSlowlyFalse)
{
    GameTime gt(TimeSpan(500), TimeSpan(250), false);
    EXPECT_FALSE(gt.getIsRunningSlowlyProperty());
}

TEST(GameTimeTest, TotalGetterReturnsReference)
{
    GameTime gt(TimeSpan(100), TimeSpan(50));
    const TimeSpan& ref = gt.getTotalGameTimeProperty();
    EXPECT_EQ(&ref, &gt.getTotalGameTimeProperty());
}

TEST(GameTimeTest, ElapsedGetterReturnsReference)
{
    GameTime gt(TimeSpan(100), TimeSpan(50));
    const TimeSpan& ref = gt.getElapsedGameTimeProperty();
    EXPECT_EQ(&ref, &gt.getElapsedGameTimeProperty());
}

TEST(GameTimeTest, ConstructorsPreserveSignedTickValuesWithoutNormalization)
{
    // FNA GameTime.cs constructors assign TimeSpans verbatim; elapsed need not be <= total.
    const GameTime two(TimeSpan::FromTicks(-17), TimeSpan::FromTicks(29));
    EXPECT_EQ(two.getTotalGameTimeProperty().getTicksProperty(), -17);
    EXPECT_EQ(two.getElapsedGameTimeProperty().getTicksProperty(), 29);
    EXPECT_FALSE(two.getIsRunningSlowlyProperty());
    const GameTime three(TimeSpan::FromTicks(7), TimeSpan::FromTicks(-31), true);
    EXPECT_EQ(three.getTotalGameTimeProperty().getTicksProperty(), 7);
    EXPECT_EQ(three.getElapsedGameTimeProperty().getTicksProperty(), -31);
    EXPECT_TRUE(three.getIsRunningSlowlyProperty());
}
