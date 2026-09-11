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
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "System/EventArgs.hpp"
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
using Microsoft::Xna::Framework::Graphics::SamplerState;

namespace
{
    class StateTestTag final : public System::Object
    {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "StateTestTag";
            return name;
        }
    };
}

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

TEST(GraphicsDeviceDefaultStateTest, CustomStatesCanBeReappliedAcrossDevices)
{
    GraphicsDevice first;
    GraphicsDevice second;

    BlendState blend;
    blend.setColorWriteChannelsProperty(ColorWriteChannels::None);
    EXPECT_NO_THROW(first.setBlendStateProperty(blend));
    EXPECT_NO_THROW(second.setBlendStateProperty(blend));
    EXPECT_NO_THROW(first.setBlendStateProperty(blend));
    EXPECT_EQ(second.getBlendStateProperty().getColorWriteChannelsProperty(),
              ColorWriteChannels::None);

    DepthStencilState depth;
    depth.setDepthBufferEnableProperty(false);
    EXPECT_NO_THROW(first.setDepthStencilStateProperty(depth));
    EXPECT_NO_THROW(second.setDepthStencilStateProperty(depth));
    EXPECT_NO_THROW(first.setDepthStencilStateProperty(depth));
    EXPECT_FALSE(second.getDepthStencilStateProperty().getDepthBufferEnableProperty());

    RasterizerState rasterizer;
    rasterizer.setCullModeProperty(CullMode::None);
    EXPECT_NO_THROW(first.setRasterizerStateProperty(rasterizer));
    EXPECT_NO_THROW(second.setRasterizerStateProperty(rasterizer));
    EXPECT_NO_THROW(first.setRasterizerStateProperty(rasterizer));
    EXPECT_EQ(second.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
}

TEST(GraphicsDeviceDefaultStateTest, AssignedStatesShareResourceSemanticsAcrossPublicAliases)
{
    GraphicsDevice device;
    StateTestTag firstTag;
    StateTestTag secondTag;
    BlendState blend;
    DepthStencilState depth;
    RasterizerState rasterizer;
    SamplerState sampler;
    blend.setNameProperty("blend-before");
    depth.setNameProperty("depth-before");
    rasterizer.setNameProperty("raster-before");
    sampler.setNameProperty("sampler-before");
    blend.setTagProperty(&firstTag);
    depth.setTagProperty(&firstTag);
    rasterizer.setTagProperty(&firstTag);
    sampler.setTagProperty(&firstTag);

    device.setBlendStateProperty(blend);
    device.setDepthStencilStateProperty(depth);
    device.setRasterizerStateProperty(rasterizer);
    device.getSamplerStatesProperty()[0] = sampler;
    BlendState& retainedBlend = device.getBlendStateProperty();
    DepthStencilState& retainedDepth = device.getDepthStencilStateProperty();
    RasterizerState& retainedRasterizer = device.getRasterizerStateProperty();
    SamplerState& retainedSampler = device.getSamplerStatesProperty()[0];

    EXPECT_EQ(blend.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(depth.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(rasterizer.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(sampler.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retainedBlend.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retainedDepth.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retainedRasterizer.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(retainedSampler.getGraphicsDeviceProperty(), &device);

    blend.setNameProperty("blend-after");
    depth.setNameProperty("depth-after");
    rasterizer.setNameProperty("raster-after");
    sampler.setNameProperty("sampler-after");
    blend.setTagProperty(&secondTag);
    depth.setTagProperty(&secondTag);
    rasterizer.setTagProperty(&secondTag);
    sampler.setTagProperty(&secondTag);
    EXPECT_EQ(retainedBlend.getNameProperty(), "blend-after");
    EXPECT_EQ(retainedDepth.getNameProperty(), "depth-after");
    EXPECT_EQ(retainedRasterizer.getNameProperty(), "raster-after");
    EXPECT_EQ(retainedSampler.getNameProperty(), "sampler-after");
    EXPECT_EQ(retainedBlend.getTagProperty(), &secondTag);
    EXPECT_EQ(retainedDepth.getTagProperty(), &secondTag);
    EXPECT_EQ(retainedRasterizer.getTagProperty(), &secondTag);
    EXPECT_EQ(retainedSampler.getTagProperty(), &secondTag);

    int blendSourceDisposing = 0;
    int blendRetainedDisposing = 0;
    System::Object* blendSender = nullptr;
    blend.Disposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++blendSourceDisposing;
        blendSender = sender;
    };
    retainedBlend.Disposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++blendRetainedDisposing;
        blendSender = sender;
    };
    retainedBlend.Dispose();
    EXPECT_TRUE(blend.getIsDisposedProperty());
    EXPECT_TRUE(retainedBlend.getIsDisposedProperty());
    EXPECT_EQ(blendSourceDisposing, 1);
    EXPECT_EQ(blendRetainedDisposing, 1);
    EXPECT_EQ(blendSender, &blend);

    depth.Dispose();
    retainedRasterizer.Dispose();
    sampler.Dispose();
    EXPECT_TRUE(retainedDepth.getIsDisposedProperty());
    EXPECT_TRUE(rasterizer.getIsDisposedProperty());
    EXPECT_TRUE(retainedSampler.getIsDisposedProperty());
}

TEST(GraphicsDeviceDefaultStateTest, AssignedStateIdentitiesOutliveSourcesAndRebindAcrossDevices)
{
    GraphicsDevice first;
    GraphicsDevice second;
    {
        BlendState blend;
        DepthStencilState depth;
        RasterizerState rasterizer;
        SamplerState sampler;
        blend.setNameProperty("retained-blend");
        depth.setNameProperty("retained-depth");
        rasterizer.setNameProperty("retained-rasterizer");
        sampler.setNameProperty("retained-sampler");
        first.setBlendStateProperty(blend);
        first.setDepthStencilStateProperty(depth);
        first.setRasterizerStateProperty(rasterizer);
        first.getSamplerStatesProperty()[0] = sampler;
    }

    second.setBlendStateProperty(first.getBlendStateProperty());
    second.setDepthStencilStateProperty(first.getDepthStencilStateProperty());
    second.setRasterizerStateProperty(first.getRasterizerStateProperty());
    second.getSamplerStatesProperty()[0] = first.getSamplerStatesProperty()[0];

    EXPECT_EQ(first.getBlendStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getDepthStencilStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getRasterizerStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getSamplerStatesProperty()[0].getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(second.getBlendStateProperty().getNameProperty(), "retained-blend");
    EXPECT_EQ(second.getDepthStencilStateProperty().getNameProperty(), "retained-depth");
    EXPECT_EQ(second.getRasterizerStateProperty().getNameProperty(), "retained-rasterizer");
    EXPECT_EQ(second.getSamplerStatesProperty()[0].getNameProperty(), "retained-sampler");
}

TEST(GraphicsDeviceDefaultStateTest, DefaultStatesSharePresetResourceSemantics)
{
    GraphicsDevice first;
    StateTestTag tag;

    EXPECT_EQ(BlendState::Opaque.getGraphicsDeviceProperty(), &first);
    EXPECT_EQ(DepthStencilState::Default.getGraphicsDeviceProperty(), &first);
    EXPECT_EQ(RasterizerState::CullCounterClockwise.getGraphicsDeviceProperty(), &first);
    EXPECT_EQ(SamplerState::LinearWrap.getGraphicsDeviceProperty(), &first);

    first.getBlendStateProperty().setNameProperty("temporary-opaque");
    first.getDepthStencilStateProperty().setNameProperty("temporary-depth");
    first.getRasterizerStateProperty().setNameProperty("temporary-rasterizer");
    first.getSamplerStatesProperty()[0].setNameProperty("temporary-linear-wrap");
    first.getSamplerStatesProperty()[0].setTagProperty(&tag);
    EXPECT_EQ(BlendState::Opaque.getNameProperty(), "temporary-opaque");
    EXPECT_EQ(DepthStencilState::Default.getNameProperty(), "temporary-depth");
    EXPECT_EQ(RasterizerState::CullCounterClockwise.getNameProperty(), "temporary-rasterizer");
    EXPECT_EQ(SamplerState::LinearWrap.getNameProperty(), "temporary-linear-wrap");
    EXPECT_EQ(first.getSamplerStatesProperty()[1].getNameProperty(), "temporary-linear-wrap");
    EXPECT_EQ(SamplerState::LinearWrap.getTagProperty(), &tag);
    EXPECT_EQ(first.getSamplerStatesProperty()[1].getTagProperty(), &tag);

    first.getBlendStateProperty().setNameProperty("BlendState.Opaque");
    first.getDepthStencilStateProperty().setNameProperty("DepthStencilState.Default");
    first.getRasterizerStateProperty().setNameProperty("RasterizerState.CullCounterClockwise");
    first.getSamplerStatesProperty()[0].setNameProperty("SamplerState.LinearWrap");
    first.getSamplerStatesProperty()[0].setTagProperty(nullptr);

    GraphicsDevice second;
    EXPECT_EQ(first.getBlendStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getDepthStencilStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getRasterizerStateProperty().getGraphicsDeviceProperty(), &second);
    EXPECT_EQ(first.getSamplerStatesProperty()[0].getGraphicsDeviceProperty(), &second);
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
