// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-100/MOD-101: the two float-render-target capabilities that the CNAEXT engine
// layer's HDR pipeline is built on. What these tests protect is the *direction* of the default: the
// shared IGraphicsRenderer::SupportsCapability() implementation answers true for almost everything,
// so a capability describing a promise this specific ("values above 1.0 survive a render-to-target")
// has to be opt-in, or every renderer in the tree would claim it the moment the enumerator existed.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace {

TEST(GraphicsCapabilityFloatRenderTargetTest, TheFloatCapabilitiesAreAnsweredWithoutThrowing)
{
    // Deliberately not asserting a fixed answer: whether a float colour buffer is renderable is a
    // property of the runtime driver, not of the renderer's name -- an ES 3.0 device without
    // GL_EXT_color_buffer_float answers false where an ES 3.2 one answers true, and both are
    // correct. What must hold everywhere is that the question is answered rather than guessed, and
    // that the answers stay internally consistent (see the agreement tests below).
    GraphicsDevice gd;

    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::FloatRenderTargets); });
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::HalfFloatRenderTargets); });
}

TEST(GraphicsCapabilityFloatRenderTargetTest, FormatsOfTheSamePrecisionAnswerTogether)
{
    // The three 32-bit float formats stand or fall together, as do the four 16-bit ones: they are
    // the same class of colour buffer differing only in channel count. A renderer answering one of
    // a group differently from its siblings would make the coarse capability meaningless.
    GraphicsDevice gd;

    const bool full = gd.SupportsCapability(GraphicsCapability::FloatRenderTargets);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single), full);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector2), full);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4), full);

    const bool half = gd.SupportsCapability(GraphicsCapability::HalfFloatRenderTargets);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle), half);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector2), half);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfVector4), half);
    EXPECT_EQ(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable), half);
}

TEST(GraphicsCapabilityFloatRenderTargetTest, NonColourNonFloatFormatsAreNotRenderTargets)
{
    // The compressed and packed formats are texture formats; nothing in CNA renders into them, and
    // claiming otherwise would put the caller back in the position MOD-100 exists to end -- asking
    // for one format and silently receiving another.
    GraphicsDevice gd;

    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt1));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt5));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Bgr565));
    EXPECT_FALSE(gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8));
}

TEST(GraphicsCapabilityFloatRenderTargetTest, ASupportedFloatFormatReallyConstructsARenderTarget)
{
    // MOD-115: the capability is a promise about RenderTarget2D, so the test has to be about
    // RenderTarget2D. Where the answer is true, an HDR target must construct and report the format
    // it was asked for; where it is false, this asserts nothing and the case below covers it.
    GraphicsDevice gd;
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "This renderer/driver does not support half-float render targets.";

    RenderTarget2D target(gd, 16, 16, false, SurfaceFormat::HdrBlendable, DepthFormat::None);

    EXPECT_EQ(target.getFormatProperty(), SurfaceFormat::HdrBlendable);
    EXPECT_EQ(target.getWidthProperty(), 16);
    EXPECT_EQ(target.getHeightProperty(), 16);
}

TEST(GraphicsCapabilityFloatRenderTargetTest, AnUnsupportedFormatIsRefusedRatherThanSubstituted)
{
    // The failure mode this whole change removes: asking for a format the renderer cannot make and
    // receiving an 8-bit Color target that looks like success.
    GraphicsDevice gd;
    if (gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt1))
        GTEST_SKIP() << "This renderer claims a compressed render-target format; test not applicable.";

    EXPECT_ANY_THROW({
        RenderTarget2D target(gd, 16, 16, false, SurfaceFormat::Dxt1, DepthFormat::None);
        (void)target.getFormatProperty();
    });
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
    EXPECT_EQ(static_cast<int>(GraphicsCapability::HalfFloatTextureLinearFiltering), 16);
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

TEST(GraphicsCapabilityFloatRenderTargetTest, HalfFloatFilteringIsAnsweredSeparatelyFromRenderability)
{
    // MOD-123: rendering into a half-float target and sampling one with a linear filter are
    // different hardware features. Keeping them separate is the point -- a pass that needs
    // filtered reads (bloom's down/upsample chain) must be able to ask about filtering alone.
    GraphicsDevice gd;

    EXPECT_NO_THROW({
        (void)gd.SupportsCapability(GraphicsCapability::HalfFloatTextureLinearFiltering);
    });
    EXPECT_NE(static_cast<int>(GraphicsCapability::HalfFloatTextureLinearFiltering),
              static_cast<int>(GraphicsCapability::HalfFloatRenderTargets));
}

} // namespace
