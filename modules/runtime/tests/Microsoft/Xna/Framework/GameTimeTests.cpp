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
