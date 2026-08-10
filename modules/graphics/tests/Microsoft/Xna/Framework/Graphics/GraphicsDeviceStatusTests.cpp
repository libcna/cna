// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDeviceStatus;

TEST(GraphicsDeviceStatusTest, NormalValue)
{
    EXPECT_EQ(static_cast<int>(GraphicsDeviceStatus::Normal), 0);
}

TEST(GraphicsDeviceStatusTest, LostValue)
{
    EXPECT_EQ(static_cast<int>(GraphicsDeviceStatus::Lost), 1);
}

TEST(GraphicsDeviceStatusTest, NotResetValue)
{
    EXPECT_EQ(static_cast<int>(GraphicsDeviceStatus::NotReset), 2);
}

TEST(GraphicsDeviceStatusTest, DistinctValues)
{
    EXPECT_NE(GraphicsDeviceStatus::Normal,   GraphicsDeviceStatus::Lost);
    EXPECT_NE(GraphicsDeviceStatus::Lost,     GraphicsDeviceStatus::NotReset);
    EXPECT_NE(GraphicsDeviceStatus::Normal,   GraphicsDeviceStatus::NotReset);
}
