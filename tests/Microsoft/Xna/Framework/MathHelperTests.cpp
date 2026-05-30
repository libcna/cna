#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/MathHelper.hpp"

using Microsoft::Xna::Framework::MathHelper;
TEST(MathHelperTest, ReturnsValueWhenWithinRange)
{
    EXPECT_EQ(MathHelper::Clamp(5, 1, 10), 5);
    EXPECT_EQ(MathHelper::Clamp(0, -5, 5), 0);
}

TEST(MathHelperTest, ClampsToMinWhenBelow)
{
    EXPECT_EQ(MathHelper::Clamp(-10, 0, 100), 0);
    EXPECT_EQ(MathHelper::Clamp(2, 3, 5), 3);
}

TEST(MathHelperTest, ClampsToMaxWhenAbove)
{
    EXPECT_EQ(MathHelper::Clamp(101, 0, 100), 100);
    EXPECT_EQ(MathHelper::Clamp(10, 0, 5), 5);
}

TEST(MathHelperTest, MinAndMaxEqual)
{
    EXPECT_EQ(MathHelper::Clamp(5, 5, 5), 5);
    EXPECT_EQ(MathHelper::Clamp(0, 5, 5), 5);
    EXPECT_EQ(MathHelper::Clamp(10, 5, 5), 5);
}
