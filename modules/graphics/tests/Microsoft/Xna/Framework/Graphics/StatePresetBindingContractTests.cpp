// SPDX-License-Identifier: MS-PL
// TQ-021: the built-in state presets are bound from construction.
//
// Oracle: Microsoft XNA 4.0 Graphics assembly. Each preset is created by a private constructor
// that stores isBound = true as its last action -- BlendState::.ctor(Blend, Blend, string)
// IL_0047-0049, DepthStencilState::.ctor(bool, bool, string) IL_002d-002f,
// RasterizerState::.ctor(CullMode, string) IL_0020-0022, SamplerState::.ctor(TextureFilter,
// TextureAddressMode, string) IL_0047-0049 -- and every public setter calls ThrowIfBound first,
// which throws a plain InvalidOperationException (BlendState::ThrowIfBound IL_0000-0036). A
// preset can therefore never be mutated, even before any GraphicsDevice exists.
//
// CNA spells C#'s reference assignment `BlendState s = BlendState.Opaque;` as copy *assignment*
// (`alias = BlendState::Opaque`, which shares the preset's state); copy *construction* is the
// CNAEXT spelling of `new BlendState()` with the preset's values and stays mutable.
//
// These tests deliberately create no GraphicsDevice: binding a preset to a device sets the same
// flag, which would hide a preset that was never bound by its own constructor. Run them filtered
// (--gtest_filter=StatePresetBindingTest.*) when using them as a mutation witness.

#include <gtest/gtest.h>

#include <array>
#include <typeinfo>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

namespace
{
    // ThrowIfBound throws exactly InvalidOperationException; ObjectDisposedException derives
    // from it, so the exact type is what distinguishes "bound" from "disposed".
    template<typename TCallable>
    void ExpectBoundRefusal(TCallable&& mutate, const char* setter)
    {
        try
        {
            mutate();
            ADD_FAILURE() << setter << " mutated a bound preset";
        }
        catch (const System::InvalidOperationException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException)) << setter;
        }
        catch (...)
        {
            ADD_FAILURE() << setter << " threw something other than InvalidOperationException";
        }
    }
}

TEST(StatePresetBindingTest, BlendPresetsRejectEverySetterWithoutADevice)
{
    struct Row
    {
        const BlendState* preset;
        const char* name;
        Blend source;
        Blend destination;
    };
    // BlendState::.cctor IL_0000-0044: (One, Zero), (One, InverseSourceAlpha),
    // (SourceAlpha, One), (SourceAlpha, InverseSourceAlpha); colour and alpha share them.
    const std::array<Row, 4> rows{{
        {&BlendState::Opaque, "Opaque", Blend::One, Blend::Zero},
        {&BlendState::AlphaBlend, "AlphaBlend", Blend::One, Blend::InverseSourceAlpha},
        {&BlendState::Additive, "Additive", Blend::SourceAlpha, Blend::One},
        {&BlendState::NonPremultiplied, "NonPremultiplied", Blend::SourceAlpha,
         Blend::InverseSourceAlpha},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);
        BlendState alias;
        alias = *row.preset;

        // Every attempted value differs from every preset, so a store-before-throw shows.
        ExpectBoundRefusal([&] { alias.setAlphaBlendFunctionProperty(BlendFunction::Max); },
                           "AlphaBlendFunction");
        ExpectBoundRefusal([&] { alias.setAlphaDestinationBlendProperty(Blend::SourceColor); },
                           "AlphaDestinationBlend");
        ExpectBoundRefusal([&] { alias.setAlphaSourceBlendProperty(Blend::DestinationAlpha); },
                           "AlphaSourceBlend");
        ExpectBoundRefusal([&] { alias.setColorBlendFunctionProperty(BlendFunction::Min); },
                           "ColorBlendFunction");
        ExpectBoundRefusal([&] { alias.setColorDestinationBlendProperty(Blend::SourceColor); },
                           "ColorDestinationBlend");
        ExpectBoundRefusal([&] { alias.setColorSourceBlendProperty(Blend::DestinationAlpha); },
                           "ColorSourceBlend");
        ExpectBoundRefusal([&] { alias.setColorWriteChannelsProperty(ColorWriteChannels::Red); },
                           "ColorWriteChannels");
        ExpectBoundRefusal([&] { alias.setColorWriteChannels1Property(ColorWriteChannels::Green); },
                           "ColorWriteChannels1");
        ExpectBoundRefusal([&] { alias.setColorWriteChannels2Property(ColorWriteChannels::Blue); },
                           "ColorWriteChannels2");
        ExpectBoundRefusal([&] { alias.setColorWriteChannels3Property(ColorWriteChannels::Alpha); },
                           "ColorWriteChannels3");
        ExpectBoundRefusal([&] { alias.setBlendFactorProperty(Color(1, 2, 3, 4)); },
                           "BlendFactor");
        ExpectBoundRefusal([&] { alias.setMultiSampleMaskProperty(0x0F); }, "MultiSampleMask");

        // Literal XNA values: the preset arguments above plus BlendState::SetDefaults
        // IL_0000-00a1 (Add, ColorWriteChannels 15, Color.White, mask -1).
        EXPECT_EQ(row.preset->getColorSourceBlendProperty(), row.source);
        EXPECT_EQ(row.preset->getAlphaSourceBlendProperty(), row.source);
        EXPECT_EQ(row.preset->getColorDestinationBlendProperty(), row.destination);
        EXPECT_EQ(row.preset->getAlphaDestinationBlendProperty(), row.destination);
        EXPECT_EQ(row.preset->getColorBlendFunctionProperty(), BlendFunction::Add);
        EXPECT_EQ(row.preset->getAlphaBlendFunctionProperty(), BlendFunction::Add);
        EXPECT_EQ(row.preset->getColorWriteChannelsProperty(), ColorWriteChannels::All);
        EXPECT_EQ(row.preset->getColorWriteChannels1Property(), ColorWriteChannels::All);
        EXPECT_EQ(row.preset->getColorWriteChannels2Property(), ColorWriteChannels::All);
        EXPECT_EQ(row.preset->getColorWriteChannels3Property(), ColorWriteChannels::All);
        EXPECT_EQ(row.preset->getBlendFactorProperty(), Color(255, 255, 255, 255));
        EXPECT_EQ(row.preset->getMultiSampleMaskProperty(), -1);
    }
}

TEST(StatePresetBindingTest, DepthStencilPresetsRejectEverySetterWithoutADevice)
{
    struct Row
    {
        const DepthStencilState* preset;
        const char* name;
        bool depthEnable;
        bool depthWriteEnable;
    };
    // DepthStencilState::.cctor passes (true, true), (true, false), (false, false).
    const std::array<Row, 3> rows{{
        {&DepthStencilState::Default, "Default", true, true},
        {&DepthStencilState::DepthRead, "DepthRead", true, false},
        {&DepthStencilState::None, "None", false, false},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);
        DepthStencilState alias;
        alias = *row.preset;

        ExpectBoundRefusal([&] { alias.setDepthBufferEnableProperty(!row.depthEnable); },
                           "DepthBufferEnable");
        ExpectBoundRefusal([&] { alias.setDepthBufferWriteEnableProperty(!row.depthWriteEnable); },
                           "DepthBufferWriteEnable");
        ExpectBoundRefusal([&] { alias.setDepthBufferFunctionProperty(CompareFunction::Never); },
                           "DepthBufferFunction");
        ExpectBoundRefusal([&] { alias.setStencilEnableProperty(true); }, "StencilEnable");
        ExpectBoundRefusal([&] { alias.setStencilFunctionProperty(CompareFunction::Equal); },
                           "StencilFunction");
        ExpectBoundRefusal([&] { alias.setStencilMaskProperty(0x0F); }, "StencilMask");
        ExpectBoundRefusal([&] { alias.setStencilWriteMaskProperty(0x0F); }, "StencilWriteMask");
        ExpectBoundRefusal([&] { alias.setReferenceStencilProperty(7); }, "ReferenceStencil");
        ExpectBoundRefusal([&] { alias.setStencilFailProperty(StencilOperation::Replace); },
                           "StencilFail");
        ExpectBoundRefusal([&] { alias.setStencilDepthBufferFailProperty(StencilOperation::Invert); },
                           "StencilDepthBufferFail");
        ExpectBoundRefusal([&] { alias.setStencilPassProperty(StencilOperation::Increment); },
                           "StencilPass");
        ExpectBoundRefusal([&] { alias.setTwoSidedStencilModeProperty(true); }, "TwoSidedStencilMode");
        ExpectBoundRefusal(
            [&] { alias.setCounterClockwiseStencilFunctionProperty(CompareFunction::Less); },
            "CounterClockwiseStencilFunction");
        ExpectBoundRefusal(
            [&] { alias.setCounterClockwiseStencilFailProperty(StencilOperation::Zero); },
            "CounterClockwiseStencilFail");
        ExpectBoundRefusal(
            [&] { alias.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Decrement); },
            "CounterClockwiseStencilDepthBufferFail");
        ExpectBoundRefusal(
            [&] { alias.setCounterClockwiseStencilPassProperty(StencilOperation::IncrementSaturation); },
            "CounterClockwiseStencilPass");

        // DepthStencilState::SetDefaults: LessEqual (3), stencil off, Always, Keep, masks -1.
        EXPECT_EQ(row.preset->getDepthBufferEnableProperty(), row.depthEnable);
        EXPECT_EQ(row.preset->getDepthBufferWriteEnableProperty(), row.depthWriteEnable);
        EXPECT_EQ(row.preset->getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
        EXPECT_FALSE(row.preset->getStencilEnableProperty());
        EXPECT_EQ(row.preset->getStencilFunctionProperty(), CompareFunction::Always);
        EXPECT_EQ(row.preset->getStencilMaskProperty(), -1);
        EXPECT_EQ(row.preset->getStencilWriteMaskProperty(), -1);
        EXPECT_EQ(row.preset->getReferenceStencilProperty(), 0);
        EXPECT_EQ(row.preset->getStencilFailProperty(), StencilOperation::Keep);
        EXPECT_EQ(row.preset->getStencilDepthBufferFailProperty(), StencilOperation::Keep);
        EXPECT_EQ(row.preset->getStencilPassProperty(), StencilOperation::Keep);
        EXPECT_FALSE(row.preset->getTwoSidedStencilModeProperty());
        EXPECT_EQ(row.preset->getCounterClockwiseStencilFunctionProperty(), CompareFunction::Always);
        EXPECT_EQ(row.preset->getCounterClockwiseStencilFailProperty(), StencilOperation::Keep);
        EXPECT_EQ(row.preset->getCounterClockwiseStencilDepthBufferFailProperty(),
                  StencilOperation::Keep);
        EXPECT_EQ(row.preset->getCounterClockwiseStencilPassProperty(), StencilOperation::Keep);
    }
}

TEST(StatePresetBindingTest, RasterizerPresetsRejectEverySetterWithoutADevice)
{
    struct Row
    {
        const RasterizerState* preset;
        const char* name;
        CullMode cullMode;
    };
    // RasterizerState::.cctor IL_0000-0021: CullMode 0, 1, 2.
    const std::array<Row, 3> rows{{
        {&RasterizerState::CullNone, "CullNone", CullMode::None},
        {&RasterizerState::CullClockwise, "CullClockwise", CullMode::CullClockwiseFace},
        {&RasterizerState::CullCounterClockwise, "CullCounterClockwise",
         CullMode::CullCounterClockwiseFace},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);
        RasterizerState alias;
        alias = *row.preset;

        const CullMode other = row.cullMode == CullMode::None ? CullMode::CullClockwiseFace
                                                              : CullMode::None;
        ExpectBoundRefusal([&] { alias.setCullModeProperty(other); }, "CullMode");
        ExpectBoundRefusal([&] { alias.setDepthBiasProperty(0.25f); }, "DepthBias");
        ExpectBoundRefusal([&] { alias.setFillModeProperty(FillMode::WireFrame); }, "FillMode");
        ExpectBoundRefusal([&] { alias.setMultiSampleAntiAliasProperty(false); },
                           "MultiSampleAntiAlias");
        ExpectBoundRefusal([&] { alias.setScissorTestEnableProperty(true); }, "ScissorTestEnable");
        ExpectBoundRefusal([&] { alias.setSlopeScaleDepthBiasProperty(2.0f); },
                           "SlopeScaleDepthBias");

        // RasterizerState::SetDefaults IL_0000-0051: Solid, no scissor, MSAA on, zero biases.
        EXPECT_EQ(row.preset->getCullModeProperty(), row.cullMode);
        EXPECT_EQ(row.preset->getDepthBiasProperty(), 0.0f);
        EXPECT_EQ(row.preset->getFillModeProperty(), FillMode::Solid);
        EXPECT_TRUE(row.preset->getMultiSampleAntiAliasProperty());
        EXPECT_FALSE(row.preset->getScissorTestEnableProperty());
        EXPECT_EQ(row.preset->getSlopeScaleDepthBiasProperty(), 0.0f);
    }
}

TEST(StatePresetBindingTest, SamplerPresetsRejectEverySetterWithoutADevice)
{
    struct Row
    {
        const SamplerState* preset;
        const char* name;
        TextureFilter filter;
        TextureAddressMode address;
    };
    // SamplerState::.cctor IL_0000-0057: (Point, Wrap), (Point, Clamp), (Linear, Wrap),
    // (Linear, Clamp), (Anisotropic, Wrap), (Anisotropic, Clamp) for U, V and W alike.
    const std::array<Row, 6> rows{{
        {&SamplerState::PointWrap, "PointWrap", TextureFilter::Point, TextureAddressMode::Wrap},
        {&SamplerState::PointClamp, "PointClamp", TextureFilter::Point, TextureAddressMode::Clamp},
        {&SamplerState::LinearWrap, "LinearWrap", TextureFilter::Linear, TextureAddressMode::Wrap},
        {&SamplerState::LinearClamp, "LinearClamp", TextureFilter::Linear,
         TextureAddressMode::Clamp},
        {&SamplerState::AnisotropicWrap, "AnisotropicWrap", TextureFilter::Anisotropic,
         TextureAddressMode::Wrap},
        {&SamplerState::AnisotropicClamp, "AnisotropicClamp", TextureFilter::Anisotropic,
         TextureAddressMode::Clamp},
    }};

    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);
        SamplerState alias;
        alias = *row.preset;

        ExpectBoundRefusal([&] { alias.setAddressUProperty(TextureAddressMode::Mirror); },
                           "AddressU");
        ExpectBoundRefusal([&] { alias.setAddressVProperty(TextureAddressMode::Mirror); },
                           "AddressV");
        ExpectBoundRefusal([&] { alias.setAddressWProperty(TextureAddressMode::Mirror); },
                           "AddressW");
        ExpectBoundRefusal([&] { alias.setFilterProperty(TextureFilter::MinPointMagLinearMipPoint); },
                           "Filter");
        ExpectBoundRefusal([&] { alias.setMaxAnisotropyProperty(9); }, "MaxAnisotropy");
        ExpectBoundRefusal([&] { alias.setMaxMipLevelProperty(3); }, "MaxMipLevel");
        ExpectBoundRefusal([&] { alias.setMipMapLevelOfDetailBiasProperty(1.5f); },
                           "MipMapLevelOfDetailBias");

        // SamplerState::SetDefaults: anisotropy 4, MaxMipLevel 0, LOD bias 0.
        EXPECT_EQ(row.preset->getFilterProperty(), row.filter);
        EXPECT_EQ(row.preset->getAddressUProperty(), row.address);
        EXPECT_EQ(row.preset->getAddressVProperty(), row.address);
        EXPECT_EQ(row.preset->getAddressWProperty(), row.address);
        EXPECT_EQ(row.preset->getMaxAnisotropyProperty(), 4);
        EXPECT_EQ(row.preset->getMaxMipLevelProperty(), 0);
        EXPECT_EQ(row.preset->getMipMapLevelOfDetailBiasProperty(), 0.0f);
    }
}

// CNAEXT copy construction is `new XxxState()` with the preset's values: the copy is unbound and
// independent, and writing it can never leak into the preset that every device shares.
TEST(StatePresetBindingTest, CopyConstructedPresetsAreMutableAndIndependent)
{
    BlendState blend(BlendState::Additive);
    EXPECT_NO_THROW(blend.setColorSourceBlendProperty(Blend::Zero));
    EXPECT_EQ(blend.getColorSourceBlendProperty(), Blend::Zero);
    EXPECT_EQ(BlendState::Additive.getColorSourceBlendProperty(), Blend::SourceAlpha);

    DepthStencilState depth(DepthStencilState::None);
    EXPECT_NO_THROW(depth.setDepthBufferEnableProperty(true));
    EXPECT_TRUE(depth.getDepthBufferEnableProperty());
    EXPECT_FALSE(DepthStencilState::None.getDepthBufferEnableProperty());

    RasterizerState rasterizer(RasterizerState::CullNone);
    EXPECT_NO_THROW(rasterizer.setCullModeProperty(CullMode::CullClockwiseFace));
    EXPECT_EQ(rasterizer.getCullModeProperty(), CullMode::CullClockwiseFace);
    EXPECT_EQ(RasterizerState::CullNone.getCullModeProperty(), CullMode::None);

    SamplerState sampler(SamplerState::PointClamp);
    EXPECT_NO_THROW(sampler.setFilterProperty(TextureFilter::Linear));
    EXPECT_EQ(sampler.getFilterProperty(), TextureFilter::Linear);
    EXPECT_EQ(SamplerState::PointClamp.getFilterProperty(), TextureFilter::Point);
}
