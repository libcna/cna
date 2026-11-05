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
    // XNA Refresh three-argument .ctor IL_0006..001b stores all three inputs.
    EXPECT_EQ(gt.getTotalGameTimeProperty().getTicksProperty(), 500);
    EXPECT_EQ(gt.getElapsedGameTimeProperty().getTicksProperty(), 250);
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

TEST(GameTimeTest, ConstructorsCopyTimeValuesAndKeepSlowFlagsIndependent)
{
    // Refresh .ctor(total,elapsed,bool) IL_0006..001b stores TimeSpan structs by value.
    TimeSpan total = TimeSpan::FromTicks(17);
    TimeSpan elapsed = TimeSpan::FromTicks(29);
    const GameTime slow(total, elapsed, true);
    const GameTime fast(total, elapsed, false);
    total = TimeSpan::FromTicks(-41);
    elapsed = TimeSpan::FromTicks(-53);
    EXPECT_EQ(slow.getTotalGameTimeProperty().getTicksProperty(), 17);
    EXPECT_EQ(slow.getElapsedGameTimeProperty().getTicksProperty(), 29);
    EXPECT_TRUE(slow.getIsRunningSlowlyProperty());
    EXPECT_EQ(fast.getTotalGameTimeProperty().getTicksProperty(), 17);
    EXPECT_EQ(fast.getElapsedGameTimeProperty().getTicksProperty(), 29);
    EXPECT_FALSE(fast.getIsRunningSlowlyProperty());
    EXPECT_EQ(total.getTicksProperty(), -41);
    EXPECT_EQ(elapsed.getTicksProperty(), -53);
}
