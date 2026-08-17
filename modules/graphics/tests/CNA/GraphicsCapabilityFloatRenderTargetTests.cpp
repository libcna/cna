// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-100/MOD-101: the two float-render-target capabilities that the CNAEXT engine
// layer's HDR pipeline is built on. What these tests protect is the *direction* of the default: the
// shared IGraphicsRenderer::SupportsCapability() implementation answers true for almost everything,
// so a capability describing a promise this specific ("values above 1.0 survive a render-to-target")
// has to be opt-in, or every renderer in the tree would claim it the moment the enumerator existed.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace {

TEST(GraphicsCapabilityFloatRenderTargetTest, FloatRenderTargetsIsAnsweredAndIsCurrentlyFalse)
{
    // No renderer in the tree creates float colour attachments yet -- every one of them falls
    // through to CreateRenderTarget2D, which produces an 8-bit Color target whatever SurfaceFormat
    // was requested. Note this is answered by GraphicsDevice from the renderer's per-format query,
    // not by the renderer's own SupportsCapability switch, whose `default: return true` would
    // otherwise claim it. The expectation flips per renderer as the Phase 1 rollout lands (EasyGL
    // in MOD-115, the committed follow-ups in MOD-1600/MOD-1604).
    GraphicsDevice gd;

    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::FloatRenderTargets));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, HalfFloatRenderTargetsIsAnsweredAndIsCurrentlyFalse)
{
    GraphicsDevice gd;

    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::HalfFloatRenderTargets));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, TheTwoFloatCapabilitiesAreIndependentEnumerators)
{
    // 16-bit float colour buffers are available on hardware where 32-bit ones are not (the GLES 3.0
    // + GL_EXT_color_buffer_half_float case), so collapsing these into one entry would force such a
    // renderer to either over-claim or under-claim. They must stay distinct values.
    EXPECT_NE(static_cast<int>(GraphicsCapability::FloatRenderTargets),
              static_cast<int>(GraphicsCapability::HalfFloatRenderTargets));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, ExistingCapabilityOrdinalsAreUnchanged)
{
    // The new entries were appended, never inserted: renderer code, traces and recorded expectations
    // across the tree compare these by value.
    EXPECT_EQ(static_cast<int>(GraphicsCapability::ThreeD), 0);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::DepthStencilBuffer), 1);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::MultiSampleAntiAliasing), 2);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::MultipleRenderTargets), 3);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::AnisotropicFiltering), 4);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::WireFrame), 5);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::OcclusionQuery), 6);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::CustomEffects), 7);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::Texture3D), 8);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::MultiStreamVertexInput), 9);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::Instancing), 10);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::StencilBuffer), 11);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::AdditiveBlending), 12);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::CompiledEffects), 13);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::FloatRenderTargets), 14);
    EXPECT_EQ(static_cast<int>(GraphicsCapability::HalfFloatRenderTargets), 15);
}

TEST(GraphicsCapabilityFloatRenderTargetTest, ThePreExistingAnswersAreUnchanged)
{
    // Guards the edit itself: the new opt-in arm in the shared default must not have disturbed how
    // the pre-existing entries are answered on the renderer this build selected.
    GraphicsDevice gd;

    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::CustomEffects));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, ColourRenderTargetsAreAlwaysSupported)
{
    // MOD-104: the per-format query's floor. Every renderer in the tree creates a Color target --
    // that is precisely what the format-ignoring CreateRenderTarget2DEXT default produces.
    GraphicsDevice gd;

    EXPECT_TRUE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Color));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, FloatFormatsAreNotYetSupportedAsRenderTargets)
{
    // MOD-103: per-format truth, which is stricter than the coarse capability. Flips format by
    // format as the Phase 1 rollout lands (EasyGL in MOD-115/MOD-116).
    GraphicsDevice gd;

    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector2));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector2));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, EachCapabilityAgreesWithItsRepresentativeFormat)
{
    // The two capabilities are summaries, not independent claims: FloatRenderTargets means RGBA32F
    // and HalfFloatRenderTargets means CNA's HdrBlendable. Asserting the agreement here means the
    // per-renderer rollout cannot flip one without the other and leave the summary lying.
    GraphicsDevice gd;

    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::FloatRenderTargets),
              gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4));
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::HalfFloatRenderTargets),
              gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable));
}

} // namespace
