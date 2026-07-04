// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

// Task 302: FNA's GraphicsDevice.cs initializes "BlendState = BlendState.Opaque". Verifies the
// default BlendState's Name matches Opaque's exactly (not just its blend-factor values, which
// happen to coincide with a plain default-constructed BlendState — see Task 292's identical
// SamplerStateCollection finding for why Name is the value that actually distinguishes them).

TEST(GraphicsDeviceDefaultStateTest, DefaultBlendStateMatchesOpaqueName)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.getBlendStateProperty().getNameProperty(), "BlendState.Opaque");
}

TEST(GraphicsDeviceDefaultStateTest, DefaultBlendStateMatchesOpaqueValues)
{
    GraphicsDevice gd;
    const BlendState& bs = gd.getBlendStateProperty();
    EXPECT_EQ(bs.getColorSourceBlendProperty(), BlendState::Opaque.getColorSourceBlendProperty());
    EXPECT_EQ(bs.getColorDestinationBlendProperty(), BlendState::Opaque.getColorDestinationBlendProperty());
    EXPECT_EQ(bs.getAlphaSourceBlendProperty(), BlendState::Opaque.getAlphaSourceBlendProperty());
    EXPECT_EQ(bs.getAlphaDestinationBlendProperty(), BlendState::Opaque.getAlphaDestinationBlendProperty());
}
