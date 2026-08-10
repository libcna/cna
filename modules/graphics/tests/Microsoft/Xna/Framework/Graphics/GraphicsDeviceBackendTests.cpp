// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsBackendType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(GraphicsDeviceBackendTest, GetGraphicsBackendTypeMatchesFreeFunction)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.GetGraphicsBackendType(), CNA::getCurrentGraphicsBackendType());
}

TEST(GraphicsDeviceBackendTest, GetGraphicsBackendNameMatchesFreeFunction)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.GetGraphicsBackendName(), CNA::getCurrentGraphicsBackendName());
}

TEST(GraphicsDeviceBackendTest, GetGraphicsBackendNameIsNotEmpty)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.GetGraphicsBackendName().empty());
}
