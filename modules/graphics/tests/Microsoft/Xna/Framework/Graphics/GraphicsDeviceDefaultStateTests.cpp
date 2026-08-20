// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RasterizerState;

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
// is a real, confirmed, intentional architectural deviation (documented in plans/plan_graphics.md
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

// Task 312: FNA's GraphicsDevice.cs initializes "DepthStencilState = DepthStencilState.Default"
// and "RasterizerState = RasterizerState.CullCounterClockwise". Found the same bug shape as
// Task 302's BlendState finding: depthStencilState_/rasterizerState_ were plain
// default-constructed members, never actually copied from the FNA-specified presets. Values
// coincided (Default's DepthBufferEnable/WriteEnable=true,true matches the plain default
// constructor's own values) so this was invisible until Task 311 gave DepthStencilState's presets
// a Name - the same "coincided until Name existed to distinguish them" pattern as Task 302.

TEST(GraphicsDeviceDefaultStateTest, DefaultDepthStencilStateMatchesDefaultName)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.getDepthStencilStateProperty().getNameProperty(), "DepthStencilState.Default");
}

TEST(GraphicsDeviceDefaultStateTest, DefaultDepthStencilStateMatchesDefaultValues)
{
    GraphicsDevice gd;
    const DepthStencilState& ds = gd.getDepthStencilStateProperty();
    EXPECT_EQ(ds.getDepthBufferEnableProperty(), DepthStencilState::Default.getDepthBufferEnableProperty());
    EXPECT_EQ(ds.getDepthBufferWriteEnableProperty(), DepthStencilState::Default.getDepthBufferWriteEnableProperty());
    EXPECT_EQ(ds.getDepthBufferFunctionProperty(), DepthStencilState::Default.getDepthBufferFunctionProperty());
}

TEST(GraphicsDeviceDefaultStateTest, DefaultRasterizerStateMatchesCullCounterClockwiseValues)
{
    GraphicsDevice gd;
    const RasterizerState& rs = gd.getRasterizerStateProperty();
    EXPECT_EQ(rs.getCullModeProperty(), RasterizerState::CullCounterClockwise.getCullModeProperty());
    EXPECT_EQ(rs.getFillModeProperty(), RasterizerState::CullCounterClockwise.getFillModeProperty());
}

// Task 322: extends the values check above to RasterizerState's full 6-property surface (not
// just CullMode/FillMode), matching the full-surface rigor DepthStencilState's Task 312 test
// already applies. CullCounterClockwise only ever diverges from a plain default-constructed
// RasterizerState in CullMode/Name (Task 321 confirmed this via FNA audit), so this is expected
// to pass trivially - but pins the full surface against regression rather than leaving it assumed.
TEST(GraphicsDeviceDefaultStateTest, DefaultRasterizerStateMatchesCullCounterClockwiseAllValues)
{
    GraphicsDevice gd;
    const RasterizerState& rs = gd.getRasterizerStateProperty();
    const RasterizerState& preset = RasterizerState::CullCounterClockwise;
    EXPECT_EQ(rs.getCullModeProperty(), preset.getCullModeProperty());
    EXPECT_EQ(rs.getDepthBiasProperty(), preset.getDepthBiasProperty());
    EXPECT_EQ(rs.getFillModeProperty(), preset.getFillModeProperty());
    EXPECT_EQ(rs.getMultiSampleAntiAliasProperty(), preset.getMultiSampleAntiAliasProperty());
    EXPECT_EQ(rs.getScissorTestEnableProperty(), preset.getScissorTestEnableProperty());
    EXPECT_EQ(rs.getSlopeScaleDepthBiasProperty(), preset.getSlopeScaleDepthBiasProperty());
}

// Task 321: now that RasterizerState::CullCounterClockwise has a Name (Task 321 fixed the last
// remaining portion of Task 866), this closes the loose end left by Task 312 (which deliberately
// skipped this check since the Name gap wasn't fixed yet).
TEST(GraphicsDeviceDefaultStateTest, DefaultRasterizerStateMatchesCullCounterClockwiseName)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.getRasterizerStateProperty().getNameProperty(),
              "RasterizerState.CullCounterClockwise");
}

// Task 330: confirmed via direct FNA source read (Graphics/States/RasterizerState.cs) that FNA has
// NO freeze/immutability enforcement for RasterizerState either - same finding as Task 310's
// BlendState/DepthStencilState/RasterizerState-generic confirmation. Mirrors
// MutatingBlendStateAfterAssignmentDoesNotAffectDevice exactly: CNA stores RasterizerState BY VALUE
// (a deliberate, project-wide pattern - see Task 869), so mutating the original object after
// assignment does NOT affect the device's already-applied copy, unlike FNA's reference semantics.
TEST(GraphicsDeviceDefaultStateTest, MutatingRasterizerStateAfterAssignmentDoesNotAffectDevice)
{
    RasterizerState custom;
    custom.setCullModeProperty(CullMode::None);

    GraphicsDevice gd;
    gd.setRasterizerStateProperty(custom);
    ASSERT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);

    // Mutate the ORIGINAL object after it has already been assigned to the device.
    custom.setCullModeProperty(CullMode::CullClockwiseFace);

    // CNA's value-copy semantics mean the device's copy is unaffected (a documented deviation from
    // FNA's reference semantics, where this mutation would be visible - see comment above).
    EXPECT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
}

// Task 319: FNA's GraphicsDevice.ReferenceStencil is a real, independent device property
// (FNA3D_Get/SetReferenceStencil) - but assigning a whole DepthStencilState (which carries its own
// ReferenceStencil field) applies that state atomically, the same way BlendState.BlendFactor is
// applied via GraphicsDevice.BlendState (Task 309). Found and fixed the same shape of bug:
// setDepthStencilStateProperty never propagated the assigned state's own ReferenceStencil into
// GraphicsDevice's own referenceStencil_, so GraphicsDevice.getReferenceStencilProperty() could
// return stale data after assigning a state with a different ReferenceStencil. Fixed by calling
// setReferenceStencilProperty from within setDepthStencilStateProperty, mirroring Task 309 exactly.
TEST(GraphicsDeviceDefaultStateTest, AssigningDepthStencilStatePropagatesReferenceStencil)
{
    GraphicsDevice gd;
    // A non-zero ReferenceStencil is meaningless (and, on a renderer with genuinely no stencil
    // plane at all, deterministically rejected -- see IGraphicsRenderer::ApplyDepthStencilState's
    // own contract) without real stencil-buffer support; this test exercises GraphicsDevice's own
    // property-propagation bookkeeping, which only runs once the renderer call it precedes
    // actually succeeds. Renderer-neutral capability gate, not an OPENVG-specific skip: any
    // stencil-less renderer (present and future) hits the same path identically.
    if (!gd.SupportsCapability(CNA::GraphicsCapability::StencilBuffer))
    {
        GTEST_SKIP() << "this renderer has no stencil buffer to hold a non-zero ReferenceStencil";
    }

    DepthStencilState custom;
    custom.setReferenceStencilProperty(42);
    gd.setDepthStencilStateProperty(custom);

    EXPECT_EQ(gd.getReferenceStencilProperty(), 42);
}
