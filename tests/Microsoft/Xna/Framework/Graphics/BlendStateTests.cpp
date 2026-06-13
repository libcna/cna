// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;

// --- Default constructor ---

TEST(BlendStateTest, DefaultColorSourceBlend)
{
    BlendState bs;
    EXPECT_EQ(bs.getColorSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, DefaultColorDestinationBlend)
{
    BlendState bs;
    EXPECT_EQ(bs.getColorDestinationBlendProperty(), Blend::Zero);
}

TEST(BlendStateTest, DefaultAlphaSourceBlend)
{
    BlendState bs;
    EXPECT_EQ(bs.getAlphaSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, DefaultAlphaDestinationBlend)
{
    BlendState bs;
    EXPECT_EQ(bs.getAlphaDestinationBlendProperty(), Blend::Zero);
}

TEST(BlendStateTest, DefaultColorBlendFunction)
{
    BlendState bs;
    EXPECT_EQ(bs.getColorBlendFunctionProperty(), BlendFunction::Add);
}

TEST(BlendStateTest, DefaultAlphaBlendFunction)
{
    BlendState bs;
    EXPECT_EQ(bs.getAlphaBlendFunctionProperty(), BlendFunction::Add);
}

TEST(BlendStateTest, DefaultColorWriteChannelsAll)
{
    BlendState bs;
    EXPECT_EQ(bs.getColorWriteChannelsProperty(),  ColorWriteChannels::All);
    EXPECT_EQ(bs.getColorWriteChannels1Property(), ColorWriteChannels::All);
    EXPECT_EQ(bs.getColorWriteChannels2Property(), ColorWriteChannels::All);
    EXPECT_EQ(bs.getColorWriteChannels3Property(), ColorWriteChannels::All);
}

TEST(BlendStateTest, DefaultBlendFactorWhite)
{
    BlendState bs;
    EXPECT_EQ(bs.getBlendFactorProperty(), Color::White);
}

TEST(BlendStateTest, DefaultMultiSampleMaskMinusOne)
{
    BlendState bs;
    EXPECT_EQ(bs.getMultiSampleMaskProperty(), -1);
}

// --- Predefined: Opaque ---

TEST(BlendStateTest, OpaqueColorSourceBlend)
{
    EXPECT_EQ(BlendState::Opaque.getColorSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, OpaqueColorDestinationBlend)
{
    EXPECT_EQ(BlendState::Opaque.getColorDestinationBlendProperty(), Blend::Zero);
}

TEST(BlendStateTest, OpaqueAlphaSourceBlend)
{
    EXPECT_EQ(BlendState::Opaque.getAlphaSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, OpaqueAlphaDestinationBlend)
{
    EXPECT_EQ(BlendState::Opaque.getAlphaDestinationBlendProperty(), Blend::Zero);
}

TEST(BlendStateTest, OpaqueBlendFunctionAdd)
{
    EXPECT_EQ(BlendState::Opaque.getColorBlendFunctionProperty(), BlendFunction::Add);
    EXPECT_EQ(BlendState::Opaque.getAlphaBlendFunctionProperty(), BlendFunction::Add);
}

// --- Predefined: AlphaBlend ---

TEST(BlendStateTest, AlphaBlendColorSourceBlend)
{
    EXPECT_EQ(BlendState::AlphaBlend.getColorSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, AlphaBlendColorDestinationBlend)
{
    EXPECT_EQ(BlendState::AlphaBlend.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
}

TEST(BlendStateTest, AlphaBlendAlphaSourceBlend)
{
    EXPECT_EQ(BlendState::AlphaBlend.getAlphaSourceBlendProperty(), Blend::One);
}

TEST(BlendStateTest, AlphaBlendAlphaDestinationBlend)
{
    EXPECT_EQ(BlendState::AlphaBlend.getAlphaDestinationBlendProperty(), Blend::InverseSourceAlpha);
}

// --- Predefined: Additive ---

TEST(BlendStateTest, AdditiveColorSourceBlend)
{
    EXPECT_EQ(BlendState::Additive.getColorSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(BlendStateTest, AdditiveColorDestinationBlend)
{
    EXPECT_EQ(BlendState::Additive.getColorDestinationBlendProperty(), Blend::One);
}

TEST(BlendStateTest, AdditiveAlphaSourceBlend)
{
    EXPECT_EQ(BlendState::Additive.getAlphaSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(BlendStateTest, AdditiveAlphaDestinationBlend)
{
    EXPECT_EQ(BlendState::Additive.getAlphaDestinationBlendProperty(), Blend::One);
}

// --- Predefined: NonPremultiplied ---

TEST(BlendStateTest, NonPremultipliedColorSourceBlend)
{
    EXPECT_EQ(BlendState::NonPremultiplied.getColorSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(BlendStateTest, NonPremultipliedColorDestinationBlend)
{
    EXPECT_EQ(BlendState::NonPremultiplied.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
}

TEST(BlendStateTest, NonPremultipliedAlphaSourceBlend)
{
    EXPECT_EQ(BlendState::NonPremultiplied.getAlphaSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(BlendStateTest, NonPremultipliedAlphaDestinationBlend)
{
    EXPECT_EQ(BlendState::NonPremultiplied.getAlphaDestinationBlendProperty(), Blend::InverseSourceAlpha);
}

// --- Setters ---

TEST(BlendStateTest, SetColorSourceBlend)
{
    BlendState bs;
    bs.setColorSourceBlendProperty(Blend::SourceAlpha);
    EXPECT_EQ(bs.getColorSourceBlendProperty(), Blend::SourceAlpha);
}

TEST(BlendStateTest, SetColorDestinationBlend)
{
    BlendState bs;
    bs.setColorDestinationBlendProperty(Blend::InverseSourceAlpha);
    EXPECT_EQ(bs.getColorDestinationBlendProperty(), Blend::InverseSourceAlpha);
}

TEST(BlendStateTest, SetAlphaBlendFunction)
{
    BlendState bs;
    bs.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
    EXPECT_EQ(bs.getAlphaBlendFunctionProperty(), BlendFunction::Subtract);
}

TEST(BlendStateTest, SetColorWriteChannels)
{
    BlendState bs;
    bs.setColorWriteChannelsProperty(ColorWriteChannels::Red);
    EXPECT_EQ(bs.getColorWriteChannelsProperty(), ColorWriteChannels::Red);
}

TEST(BlendStateTest, SetBlendFactor)
{
    BlendState bs;
    Color c(128, 64, 32, 255);
    bs.setBlendFactorProperty(c);
    EXPECT_EQ(bs.getBlendFactorProperty(), c);
}

TEST(BlendStateTest, SetMultiSampleMask)
{
    BlendState bs;
    bs.setMultiSampleMaskProperty(0xFF);
    EXPECT_EQ(bs.getMultiSampleMaskProperty(), 0xFF);
}
