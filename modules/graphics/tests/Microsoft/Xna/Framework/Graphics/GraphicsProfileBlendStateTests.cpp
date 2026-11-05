// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <typeinfo>
#include "System/ObjectDisposedException.hpp"

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
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
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
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
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
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
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
    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
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

TEST(GraphicsProfileBlendStateTest, EveryBlendFactorPairUsesXnasAlphaEquivalenceClasses)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    // XNA GraphicsHelpers.AdjustAlphaBlend IL_0024-002b maps colour and alpha variants
    // into these equivalence classes. IsSeparateBlend IL_0000-0011 compares those classes.
    constexpr int classes[] = {0, 1, 4, 5, 4, 5, 8, 9, 8, 9, 10, 11, 12};
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), profile,
                              PresentationParameters::CreateWindowedEXT(8, 8));
        for (const bool destination : {false, true})
        for (int color = 0; color != 13; ++color)
        for (int alpha = 0; alpha != 13; ++alpha)
        {
            SCOPED_TRACE(static_cast<int>(profile));
            SCOPED_TRACE(destination);
            SCOPED_TRACE(color);
            SCOPED_TRACE(alpha);
            device.setBlendStateProperty(BlendState::Opaque);
            BlendState state;
            if (destination)
            {
                state.setColorDestinationBlendProperty(static_cast<Blend>(color));
                state.setAlphaDestinationBlendProperty(static_cast<Blend>(alpha));
            }
            else
            {
                state.setColorSourceBlendProperty(static_cast<Blend>(color));
                state.setAlphaSourceBlendProperty(static_cast<Blend>(alpha));
            }
            // BlendState.Apply IL_00c2-00f9 independently excludes destination saturation.
            const bool refused = profile == GraphicsProfile::Reach &&
                (classes[color] != classes[alpha] || (destination && (color == 12 || alpha == 12)));
            if (refused)
            {
                EXPECT_THROW(device.setBlendStateProperty(state), System::NotSupportedException);
                EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);
                EXPECT_EQ(device.getBlendStateProperty().getAlphaSourceBlendProperty(), Blend::One);
                EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(), Blend::Zero);
                EXPECT_EQ(device.getBlendStateProperty().getAlphaDestinationBlendProperty(), Blend::Zero);
                // Refused state objects have not become bound/read-only.
                EXPECT_NO_THROW(state.setColorSourceBlendProperty(Blend::Zero));
                EXPECT_EQ(state.getColorSourceBlendProperty(), Blend::Zero);
            }
            else
            {
                device.setBlendStateProperty(state);
                const auto& actual = device.getBlendStateProperty();
                EXPECT_EQ(destination ? actual.getColorDestinationBlendProperty() : actual.getColorSourceBlendProperty(), static_cast<Blend>(color));
                EXPECT_EQ(destination ? actual.getAlphaDestinationBlendProperty() : actual.getAlphaSourceBlendProperty(), static_cast<Blend>(alpha));
            }
        }
    }
}

TEST(GraphicsProfileBlendStateTest, SeparateBlendFunctionsAndMinMaxFactorsAreCheckedPerChannel)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    // XNA BlendState.Apply IL_009f-00bd checks the separate functions; IL_00ff-0169
    // checks Min/Max source AND destination on each colour/alpha channel, at BOTH profiles.
    for (const auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), profile,
                              PresentationParameters::CreateWindowedEXT(8, 8));
        for (const auto function : {BlendFunction::Add, BlendFunction::Subtract, BlendFunction::ReverseSubtract,
                                   BlendFunction::Min, BlendFunction::Max})
        {
            BlendState matched;
            matched.setColorBlendFunctionProperty(function);
            matched.setAlphaBlendFunctionProperty(function);
            matched.setColorDestinationBlendProperty(Blend::One);
            matched.setAlphaDestinationBlendProperty(Blend::One);
            device.setBlendStateProperty(matched);
            EXPECT_EQ(device.getBlendStateProperty().getColorBlendFunctionProperty(), function);
            EXPECT_EQ(device.getBlendStateProperty().getAlphaBlendFunctionProperty(), function);
        }
        BlendState separate;
        separate.setAlphaBlendFunctionProperty(BlendFunction::ReverseSubtract);
        if (profile == GraphicsProfile::Reach)
            EXPECT_THROW(device.setBlendStateProperty(separate), System::NotSupportedException);
        else
        {
            device.setBlendStateProperty(separate);
            EXPECT_EQ(device.getBlendStateProperty().getColorBlendFunctionProperty(), BlendFunction::Add);
            EXPECT_EQ(device.getBlendStateProperty().getAlphaBlendFunctionProperty(), BlendFunction::ReverseSubtract);
        }
        for (const auto function : {BlendFunction::Min, BlendFunction::Max})
        for (const bool source : {false, true})
        {
            BlendState invalid;
            invalid.setColorBlendFunctionProperty(function);
            invalid.setAlphaBlendFunctionProperty(function);
            invalid.setColorDestinationBlendProperty(Blend::One);
            invalid.setAlphaDestinationBlendProperty(Blend::One);
            if (source)
            {
                invalid.setColorSourceBlendProperty(Blend::Zero);
                invalid.setAlphaSourceBlendProperty(Blend::Zero);
            }
            else
            {
                invalid.setColorDestinationBlendProperty(Blend::Zero);
                invalid.setAlphaDestinationBlendProperty(Blend::Zero);
            }
            device.setBlendStateProperty(BlendState::Opaque);
            EXPECT_THROW(device.setBlendStateProperty(invalid), System::NotSupportedException);
            EXPECT_EQ(device.getBlendStateProperty().getColorBlendFunctionProperty(), BlendFunction::Add);
            EXPECT_EQ(device.getBlendStateProperty().getAlphaBlendFunctionProperty(), BlendFunction::Add);
        }
        if (profile == GraphicsProfile::HiDef)
        {
            // Isolate the alpha-channel Min/Max gate from the colour-channel gate.
            for (const bool alpha : {false, true})
            {
                BlendState invalid;
                if (alpha) invalid.setAlphaBlendFunctionProperty(BlendFunction::Min);
                else invalid.setColorBlendFunctionProperty(BlendFunction::Max);
                EXPECT_THROW(device.setBlendStateProperty(invalid), System::NotSupportedException);
            }
        }
    }
}

TEST(GraphicsProfileBlendStateTest, RefusalsNameTheFirstUnsupportedBlendChannel)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
    // Oracle: XNA BlendState.Apply IL_0030-0169, FrameworkResources
    // ProfileNoSeparateAlphaBlend/ProfileFeatureNotSupported/ProfileNoMinMaxSrcDestBlend.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
                          PresentationParameters::CreateWindowedEXT(8, 8));
    const std::string prefix = "XNA Framework Reach profile ";
    auto refuses = [&](BlendState& state, const std::string& detail)
    {
        try { device.setBlendStateProperty(state); ADD_FAILURE() << "Invalid blend accepted"; }
        catch (const System::NotSupportedException& error)
        {
            EXPECT_EQ(error.getMessageProperty(), prefix + detail);
        }
        EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);
        EXPECT_EQ(device.getBlendStateProperty().getAlphaBlendFunctionProperty(), BlendFunction::Add);
    };
    BlendState source;
    source.setAlphaSourceBlendProperty(Blend::Zero);
    source.setAlphaDestinationBlendProperty(Blend::One);
    refuses(source, "does not support separate alpha blend. BlendState.ColorSourceBlend and AlphaSourceBlend must be set to the same value.");
    BlendState destination;
    destination.setAlphaDestinationBlendProperty(Blend::One);
    destination.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
    refuses(destination, "does not support separate alpha blend. BlendState.ColorDestinationBlend and AlphaDestinationBlend must be set to the same value.");
    BlendState function;
    function.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
    refuses(function, "does not support separate alpha blend. BlendState.ColorBlendFunction and AlphaBlendFunction must be set to the same value.");
    BlendState saturation;
    saturation.setColorDestinationBlendProperty(Blend::SourceAlphaSaturation);
    saturation.setAlphaDestinationBlendProperty(Blend::SourceAlphaSaturation);
    refuses(saturation, "does not support ColorDestinationBlend = Blend.SourceAlphaSaturation.");
    BlendState minmax;
    minmax.setColorBlendFunctionProperty(BlendFunction::Min);
    minmax.setAlphaBlendFunctionProperty(BlendFunction::Min);
    refuses(minmax, "requires ColorSourceBlend and ColorDestinationBlend to be One when using ColorBlendFunction Min or Max.");
}

// Refresh BlendState::Apply IL_0000-001c checks disposal before IL_0029-0169 profile checks.
TEST(GraphicsProfileBlendStateTest, DisposedBlendRefusalPrecedesProfileValidation)
{
    GraphicsDevice device;
    for (GraphicsProfile profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        for (bool separateAlpha : {false, true})
        {
            SCOPED_TRACE(static_cast<int>(profile));
            SCOPED_TRACE(separateAlpha);
            BlendState state;
            if (separateAlpha)
                state.setAlphaSourceBlendProperty(Blend::Zero); // Illegal separate alpha on Reach.
            else
                state.setColorBlendFunctionProperty(BlendFunction::Min); // One/Zero illegal Min.
            state.setNameProperty("not-the-type-name");
            state.Dispose();
            try { device.setBlendStateProperty(state); ADD_FAILURE(); }
            catch (const System::ObjectDisposedException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::ObjectDisposedException));
                EXPECT_EQ(error.getObjectNameProperty(), "BlendState");
            }
            EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(), Blend::One);
            EXPECT_EQ(device.getBlendStateProperty().getAlphaSourceBlendProperty(), Blend::One);
            EXPECT_EQ(device.getBlendStateProperty().getColorBlendFunctionProperty(), BlendFunction::Add);
            EXPECT_EQ(state.getGraphicsDeviceProperty(), nullptr);
            // Refusal must not bind the disposed, previously unbound state.
            state.setColorSourceBlendProperty(Blend::Zero);
            EXPECT_EQ(state.getColorSourceBlendProperty(), Blend::Zero);
        }
    }
}
