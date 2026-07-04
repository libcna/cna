// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using Microsoft::Xna::Framework::Graphics::Blend;
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

// Task 310: FNA's BlendState/DepthStencilState/RasterizerState have no freeze/immutability
// enforcement at all (confirmed via FNA source: no exception, no frozen flag anywhere in
// States/BlendState.cs et al.) - so there is no "throws if mutated after first use" behavior to
// replicate. But FNA's GraphicsDevice.BlendState setter ("nextBlend = value;") stores a
// *reference* to the same C# object, since BlendState is a reference type - mutating that same
// object afterward changes what the device applies on the next Draw. CNA's GraphicsDevice stores
// BlendState by VALUE ("blendState_ = value;" copies), a deliberate, project-wide pattern shared by
// DepthStencilState/RasterizerState/SamplerStateCollection - so mutating the original object after
// assignment does NOT affect the device's already-applied copy in CNA, unlike real XNA/FNA. This
// is a real, confirmed, intentional architectural deviation (documented in GRAPHICS_TASKS.md
// Task 869), not a bug: no game code observed in this codebase relies on post-assignment mutation,
// and matching FNA's reference-aliasing exactly would require every state property to become a
// reference/pointer type project-wide.
TEST(GraphicsDeviceDefaultStateTest, MutatingBlendStateAfterAssignmentDoesNotAffectDevice)
{
    BlendState custom;
    custom.setColorSourceBlendProperty(Blend::One);

    GraphicsDevice gd;
    gd.setBlendStateProperty(custom);
    ASSERT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);

    // Mutate the ORIGINAL object after it has already been assigned to the device.
    custom.setColorSourceBlendProperty(Blend::Zero);

    // CNA's value-copy semantics mean the device's copy is unaffected (a documented deviation from
    // FNA's reference semantics, where this mutation would be visible - see comment above).
    EXPECT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);
}
