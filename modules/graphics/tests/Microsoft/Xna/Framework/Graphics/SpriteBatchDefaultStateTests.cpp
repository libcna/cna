// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(SpriteBatchDefaultStateTest, EveryBeginOverloadPublishesDefaultsAtItsStateBoundary)
{
    // Refresh Begin wrappers + Begin(7) IL_0048-0066; End IL_0013-0022.
    // SetRenderState IL_001b-0026,0046-0051,0071-007c,00a2-00b3 selects
    // AlphaBlend/None/CullCounterClockwise/LinearClamp and only sampler slot zero.
    GraphicsDevice device;
    SpriteBatch batch(device);
    const auto expectDefaults = [&] {
        const auto& blend = device.getBlendStateProperty();
        EXPECT_EQ(blend.getColorSourceBlendProperty(), Blend::One);
        EXPECT_EQ(blend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
        EXPECT_EQ(blend.getAlphaSourceBlendProperty(), Blend::One);
        EXPECT_EQ(blend.getAlphaDestinationBlendProperty(), Blend::InverseSourceAlpha);
        EXPECT_FALSE(device.getDepthStencilStateProperty().getDepthBufferEnableProperty());
        EXPECT_FALSE(device.getDepthStencilStateProperty().getDepthBufferWriteEnableProperty());
        EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(),
                  CullMode::CullCounterClockwiseFace);
        const auto& sampler = device.getSamplerStatesProperty()[0];
        EXPECT_EQ(sampler.getFilterProperty(), TextureFilter::Linear);
        EXPECT_EQ(sampler.getAddressUProperty(), TextureAddressMode::Clamp);
        EXPECT_EQ(sampler.getAddressVProperty(), TextureAddressMode::Clamp);
        EXPECT_EQ(sampler.getAddressWProperty(), TextureAddressMode::Clamp);
    };
    for (int overload = 0; overload < 8; ++overload)
    for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
    {
        if (overload == 0 && mode == SpriteSortMode::Immediate) continue; // Begin() is Deferred.
        SCOPED_TRACE(overload);
        SCOPED_TRACE(static_cast<int>(mode));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
        device.getSamplerStatesProperty()[1] = SamplerState::PointWrap;
        switch (overload)
        {
        case 0: batch.Begin(); break;
        case 1: batch.Begin(mode, BlendState::AlphaBlend); break;
        case 2: batch.Begin(mode, BlendState::AlphaBlend, nullptr, nullptr, nullptr); break;
        case 3: batch.Begin(mode, nullptr, nullptr, nullptr, nullptr); break;
        case 4: batch.Begin(mode, BlendState::AlphaBlend, nullptr, nullptr, nullptr, nullptr); break;
        case 5: batch.Begin(mode, nullptr, nullptr, nullptr, nullptr, nullptr); break;
        case 6: batch.Begin(mode, BlendState::AlphaBlend, nullptr, nullptr, nullptr, nullptr,
                            Matrix::getIdentityProperty()); break;
        case 7: batch.Begin(mode, nullptr, nullptr, nullptr, nullptr, nullptr,
                            Matrix::getIdentityProperty()); break;
        }
        if (mode == SpriteSortMode::Immediate) expectDefaults();
        else
        {
            EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(), Blend::Zero);
            EXPECT_TRUE(device.getDepthStencilStateProperty().getDepthBufferEnableProperty());
            EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
            EXPECT_EQ(device.getSamplerStatesProperty()[0].getFilterProperty(), TextureFilter::Point);
            EXPECT_EQ(device.getSamplerStatesProperty()[0].getAddressUProperty(), TextureAddressMode::Wrap);
        }
        batch.End(); // An EMPTY batch still publishes defaults and does not restore old states.
        expectDefaults();
        EXPECT_EQ(device.getSamplerStatesProperty()[1].getFilterProperty(), TextureFilter::Point);
        EXPECT_EQ(device.getSamplerStatesProperty()[1].getAddressUProperty(), TextureAddressMode::Wrap);
    }
}

TEST(SpriteBatchDefaultStateTest, ImmediateEndLeavesStatesChangedAfterBegin)
{
    // Refresh End IL_0013-0038 skips SetRenderState in Immediate and restores nothing.
    GraphicsDevice device;
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::PointWrap;
    batch.End();
    EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(), Blend::Zero);
    EXPECT_TRUE(device.getDepthStencilStateProperty().getDepthBufferEnableProperty());
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), CullMode::None);
    EXPECT_EQ(device.getSamplerStatesProperty()[0].getFilterProperty(), TextureFilter::Point);
    EXPECT_EQ(device.getSamplerStatesProperty()[0].getAddressUProperty(), TextureAddressMode::Wrap);
}
}
