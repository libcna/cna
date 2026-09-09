// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
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

// SOFTWARE-232: recovered Microsoft XNA state objects set isBound on first Apply and every
// property setter subsequently throws InvalidOperationException. FNA's mutable behavior is a
// compatibility divergence, not the authority for this contract.
TEST(GraphicsDeviceDefaultStateTest, BoundBlendStateRejectsMutationThroughSourceAndDevice)
{
    BlendState custom;
    custom.setColorSourceBlendProperty(Blend::One);

    GraphicsDevice gd;
    gd.setBlendStateProperty(custom);
    ASSERT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);

    EXPECT_THROW(custom.setColorSourceBlendProperty(Blend::Zero),
                 System::InvalidOperationException);
    EXPECT_THROW(gd.getBlendStateProperty().setColorSourceBlendProperty(Blend::Zero),
                 System::InvalidOperationException);
    EXPECT_EQ(gd.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);
}

TEST(GraphicsDeviceDefaultStateTest, BoundBlendStateRejectsEveryPropertySetter)
{
    BlendState custom;
    GraphicsDevice gd;
    gd.setBlendStateProperty(custom);

    EXPECT_THROW(custom.setAlphaBlendFunctionProperty(BlendFunction::Subtract), System::InvalidOperationException);
    EXPECT_THROW(custom.setAlphaDestinationBlendProperty(Blend::One), System::InvalidOperationException);
    EXPECT_THROW(custom.setAlphaSourceBlendProperty(Blend::Zero), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorDestinationBlendProperty(Blend::One), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorSourceBlendProperty(Blend::Zero), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorWriteChannelsProperty(ColorWriteChannels::None), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorWriteChannels1Property(ColorWriteChannels::None), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorWriteChannels2Property(ColorWriteChannels::None), System::InvalidOperationException);
    EXPECT_THROW(custom.setColorWriteChannels3Property(ColorWriteChannels::None), System::InvalidOperationException);
    EXPECT_THROW(custom.setBlendFactorProperty(Microsoft::Xna::Framework::Color::Black), System::InvalidOperationException);
    EXPECT_THROW(custom.setMultiSampleMaskProperty(1), System::InvalidOperationException);
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

TEST(GraphicsDeviceDefaultStateTest, BoundRasterizerStateRejectsMutationThroughSourceAndDevice)
{
    RasterizerState custom;
    custom.setCullModeProperty(CullMode::None);

    GraphicsDevice gd;
    gd.setRasterizerStateProperty(custom);
    ASSERT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);

    EXPECT_THROW(custom.setCullModeProperty(CullMode::CullClockwiseFace),
                 System::InvalidOperationException);
    EXPECT_THROW(gd.getRasterizerStateProperty().setCullModeProperty(CullMode::CullClockwiseFace),
                 System::InvalidOperationException);
    EXPECT_EQ(gd.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
}

TEST(GraphicsDeviceDefaultStateTest, BoundRasterizerStateRejectsEveryPropertySetter)
{
    RasterizerState custom;
    GraphicsDevice gd;
    gd.setRasterizerStateProperty(custom);

    EXPECT_THROW(custom.setCullModeProperty(CullMode::None), System::InvalidOperationException);
    EXPECT_THROW(custom.setDepthBiasProperty(1.0f), System::InvalidOperationException);
    EXPECT_THROW(custom.setFillModeProperty(Microsoft::Xna::Framework::Graphics::FillMode::WireFrame), System::InvalidOperationException);
    EXPECT_THROW(custom.setMultiSampleAntiAliasProperty(false), System::InvalidOperationException);
    EXPECT_THROW(custom.setScissorTestEnableProperty(true), System::InvalidOperationException);
    EXPECT_THROW(custom.setSlopeScaleDepthBiasProperty(1.0f), System::InvalidOperationException);
}

TEST(GraphicsDeviceDefaultStateTest, BoundDepthStencilStateRejectsMutationThroughSourceAndDevice)
{
    DepthStencilState custom;
    custom.setDepthBufferWriteEnableProperty(false);

    GraphicsDevice gd;
    gd.setDepthStencilStateProperty(custom);
    ASSERT_FALSE(gd.getDepthStencilStateProperty().getDepthBufferWriteEnableProperty());

    EXPECT_THROW(custom.setDepthBufferWriteEnableProperty(true),
                 System::InvalidOperationException);
    EXPECT_THROW(gd.getDepthStencilStateProperty().setDepthBufferWriteEnableProperty(true),
                 System::InvalidOperationException);
    EXPECT_FALSE(gd.getDepthStencilStateProperty().getDepthBufferWriteEnableProperty());
}

TEST(GraphicsDeviceDefaultStateTest, BoundDepthStencilStateRejectsEveryPropertySetter)
{
    DepthStencilState custom;
    GraphicsDevice gd;
    gd.setDepthStencilStateProperty(custom);

    using Microsoft::Xna::Framework::Graphics::CompareFunction;
    using Microsoft::Xna::Framework::Graphics::StencilOperation;
    EXPECT_THROW(custom.setDepthBufferEnableProperty(false), System::InvalidOperationException);
    EXPECT_THROW(custom.setDepthBufferWriteEnableProperty(false), System::InvalidOperationException);
    EXPECT_THROW(custom.setDepthBufferFunctionProperty(CompareFunction::Less), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilEnableProperty(true), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilFunctionProperty(CompareFunction::Equal), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilMaskProperty(1), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilWriteMaskProperty(1), System::InvalidOperationException);
    EXPECT_THROW(custom.setReferenceStencilProperty(1), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilFailProperty(StencilOperation::Replace), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilDepthBufferFailProperty(StencilOperation::Replace), System::InvalidOperationException);
    EXPECT_THROW(custom.setStencilPassProperty(StencilOperation::Replace), System::InvalidOperationException);
    EXPECT_THROW(custom.setTwoSidedStencilModeProperty(true), System::InvalidOperationException);
    EXPECT_THROW(custom.setCounterClockwiseStencilFunctionProperty(CompareFunction::Equal), System::InvalidOperationException);
    EXPECT_THROW(custom.setCounterClockwiseStencilFailProperty(StencilOperation::Replace), System::InvalidOperationException);
    EXPECT_THROW(custom.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Replace), System::InvalidOperationException);
    EXPECT_THROW(custom.setCounterClockwiseStencilPassProperty(StencilOperation::Replace), System::InvalidOperationException);
}

TEST(GraphicsDeviceDefaultStateTest, DisposedStatesAreRejectedBeforeBinding)
{
    GraphicsDevice gd;

    BlendState blend;
    blend.Dispose();
    EXPECT_THROW(gd.setBlendStateProperty(blend), System::ObjectDisposedException);

    DepthStencilState depthStencil;
    depthStencil.Dispose();
    EXPECT_THROW(gd.setDepthStencilStateProperty(depthStencil), System::ObjectDisposedException);

    RasterizerState rasterizer;
    rasterizer.Dispose();
    EXPECT_THROW(gd.setRasterizerStateProperty(rasterizer), System::ObjectDisposedException);
}

TEST(GraphicsDeviceDefaultStateTest, DisposalAfterBindingInvalidatesAssignedPayload)
{
    GraphicsDevice gd;

    BlendState blend;
    gd.setBlendStateProperty(blend);
    blend.Dispose();
    EXPECT_THROW(gd.setBlendStateProperty(gd.getBlendStateProperty()),
                 System::ObjectDisposedException);

    DepthStencilState depth;
    gd.setDepthStencilStateProperty(depth);
    depth.Dispose();
    EXPECT_THROW(gd.setDepthStencilStateProperty(gd.getDepthStencilStateProperty()),
                 System::ObjectDisposedException);

    RasterizerState rasterizer;
    gd.setRasterizerStateProperty(rasterizer);
    rasterizer.Dispose();
    EXPECT_THROW(gd.setRasterizerStateProperty(gd.getRasterizerStateProperty()),
                 System::ObjectDisposedException);
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
