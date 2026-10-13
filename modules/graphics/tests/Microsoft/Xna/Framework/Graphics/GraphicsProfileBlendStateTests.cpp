// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "System/NotSupportedException.hpp"

using namespace CNA::Testing::Renderers;

using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;

TEST(GraphicsProfileBlendStateTest, ReachRejectsSeparateAlphaAndDestinationSaturation)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);

    BlendState separateSource;
    separateSource.setAlphaSourceBlendProperty(Blend::Zero);
    EXPECT_THROW(device.setBlendStateProperty(separateSource), System::NotSupportedException);

    BlendState separateDestination;
    separateDestination.setAlphaDestinationBlendProperty(Blend::One);
    EXPECT_THROW(device.setBlendStateProperty(separateDestination), System::NotSupportedException);

    BlendState separateFunction;
    separateFunction.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
    EXPECT_THROW(device.setBlendStateProperty(separateFunction), System::NotSupportedException);

    BlendState saturatedDestination;
    saturatedDestination.setColorDestinationBlendProperty(Blend::SourceAlphaSaturation);
    saturatedDestination.setAlphaDestinationBlendProperty(Blend::SourceAlphaSaturation);
    EXPECT_THROW(device.setBlendStateProperty(saturatedDestination), System::NotSupportedException);

    EXPECT_EQ(Blend::One, device.getBlendStateProperty().getColorSourceBlendProperty());
    EXPECT_EQ(Blend::Zero, device.getBlendStateProperty().getColorDestinationBlendProperty());
}

TEST(GraphicsProfileBlendStateTest, ReachRecognizesEquivalentColorAndAlphaFactors)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    BlendState state;
    state.setColorSourceBlendProperty(Blend::SourceColor);
    state.setAlphaSourceBlendProperty(Blend::SourceAlpha);
    state.setColorDestinationBlendProperty(Blend::DestinationColor);
    state.setAlphaDestinationBlendProperty(Blend::DestinationAlpha);

    EXPECT_NO_THROW(device.setBlendStateProperty(state));
}

TEST(GraphicsProfileBlendStateTest, HiDefAllowsSeparateAlphaAndDestinationSaturation)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);
    BlendState state;
    state.setAlphaSourceBlendProperty(Blend::Zero);
    state.setColorDestinationBlendProperty(Blend::SourceAlphaSaturation);
    state.setAlphaDestinationBlendProperty(Blend::Zero);

    EXPECT_NO_THROW(device.setBlendStateProperty(state));
}

TEST(GraphicsProfileBlendStateTest, MinAndMaxRequireOneOneFactorsInBothProfiles)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    PresentationParameters parameters;
    GraphicsDevice reach(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach, parameters);
    GraphicsDevice hiDef(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    BlendState illegal;
    illegal.setColorBlendFunctionProperty(BlendFunction::Min);
    illegal.setAlphaBlendFunctionProperty(BlendFunction::Min);
    EXPECT_THROW(reach.setBlendStateProperty(illegal), System::NotSupportedException);
    EXPECT_THROW(hiDef.setBlendStateProperty(illegal), System::NotSupportedException);

    BlendState legal;
    legal.setColorBlendFunctionProperty(BlendFunction::Max);
    legal.setAlphaBlendFunctionProperty(BlendFunction::Max);
    legal.setColorDestinationBlendProperty(Blend::One);
    legal.setAlphaDestinationBlendProperty(Blend::One);
    EXPECT_NO_THROW(reach.setBlendStateProperty(legal));
    EXPECT_NO_THROW(hiDef.setBlendStateProperty(legal));
}
