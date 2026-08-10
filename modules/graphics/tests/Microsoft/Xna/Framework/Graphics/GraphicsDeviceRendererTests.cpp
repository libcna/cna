// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererTypeMatchesFreeFunction)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.GetGraphicsRendererType(), CNA::getCurrentGraphicsRendererType());
}

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererNameMatchesFreeFunction)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.GetGraphicsRendererName(), CNA::getCurrentGraphicsRendererName());
}

TEST(GraphicsDeviceRendererTest, GetGraphicsRendererNameIsNotEmpty)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.GetGraphicsRendererName().empty());
}
