// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>

#include "CNA/Internal/Renderers/Common/XnaStateConversion.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace CNA::Internal::Renderers;
using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters SmallBackBuffer(const DepthFormat depthFormat = DepthFormat::None)
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(8);
        parameters.setBackBufferHeightProperty(8);
        parameters.setDepthStencilFormatProperty(depthFormat);
        return parameters;
    }

    GraphicsDevice MakeDevice(const DepthFormat depthFormat = DepthFormat::None)
    {
        return GraphicsDevice(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            SmallBackBuffer(depthFormat));
    }

    void ConfigureVertexColorEffect(BasicEffect& effect)
    {
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
    }

    void DrawFullScreen(GraphicsDevice& device, const Color& color, const float depth)
    {
        BasicEffect effect(device);
        ConfigureVertexColorEffect(effect);
        const std::array vertices = {
            VertexPositionColor(Vector3(-1.0f,  1.0f, depth), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, depth), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, depth), color),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);
    }

    void DrawOppositeWindingTriangles(GraphicsDevice& device, const Color& color)
    {
        BasicEffect effect(device);
        ConfigureVertexColorEffect(effect);
        const std::array vertices = {
            VertexPositionColor(Vector3(-0.95f, -0.8f, 0.0f), color),
            VertexPositionColor(Vector3(-0.05f, -0.8f, 0.0f), color),
            VertexPositionColor(Vector3(-0.50f,  0.8f, 0.0f), color),
            VertexPositionColor(Vector3( 0.05f, -0.8f, 0.0f), color),
            VertexPositionColor(Vector3( 0.50f,  0.8f, 0.0f), color),
            VertexPositionColor(Vector3( 0.95f, -0.8f, 0.0f), color),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);
    }

    Color ReadPixel(GraphicsDevice& device, const int x, const int y)
    {
        Color pixel = Color::Transparent;
        const Rectangle rectangle(x, y, 1, 1);
        device.GetBackBufferData(&rectangle, &pixel, 0, 1);
        return pixel;
    }
}

TEST(StateEnumFallbackTest, RendererSeamUsesRecoveredMicrosoftFallbackOrdinals)
{
    EXPECT_EQ(NormalizeXnaBlendOrdinal(-1), static_cast<int>(Blend::Zero));
    EXPECT_EQ(NormalizeXnaBlendOrdinal(13), static_cast<int>(Blend::Zero));
    EXPECT_EQ(NormalizeXnaBlendFunctionOrdinal(-1), static_cast<int>(BlendFunction::Add));
    EXPECT_EQ(NormalizeXnaBlendFunctionOrdinal(5), static_cast<int>(BlendFunction::Add));
    EXPECT_EQ(NormalizeXnaCompareFunctionOrdinal(-1), static_cast<int>(CompareFunction::Always));
    EXPECT_EQ(NormalizeXnaCompareFunctionOrdinal(8), static_cast<int>(CompareFunction::Always));
    EXPECT_EQ(NormalizeXnaStencilOperationOrdinal(-1), static_cast<int>(StencilOperation::Keep));
    EXPECT_EQ(NormalizeXnaStencilOperationOrdinal(8), static_cast<int>(StencilOperation::Keep));
    EXPECT_EQ(NormalizeXnaCullModeOrdinal(-1), static_cast<int>(CullMode::None));
    EXPECT_EQ(NormalizeXnaCullModeOrdinal(3), static_cast<int>(CullMode::None));
    EXPECT_EQ(NormalizeXnaFillModeOrdinal(-1), static_cast<int>(FillMode::Solid));
    EXPECT_EQ(NormalizeXnaFillModeOrdinal(2), static_cast<int>(FillMode::Solid));
    EXPECT_EQ(NormalizeXnaTextureFilterOrdinal(-1), static_cast<int>(TextureFilter::Linear));
    EXPECT_EQ(NormalizeXnaTextureFilterOrdinal(9), static_cast<int>(TextureFilter::Linear));
    EXPECT_EQ(NormalizeXnaTextureAddressModeOrdinal(-1),
              static_cast<int>(TextureAddressMode::Wrap));
    EXPECT_EQ(NormalizeXnaTextureAddressModeOrdinal(3),
              static_cast<int>(TextureAddressMode::Wrap));

    for (int value = 0; value <= 12; ++value) EXPECT_EQ(NormalizeXnaBlendOrdinal(value), value);
    for (int value = 0; value <= 4; ++value)
        EXPECT_EQ(NormalizeXnaBlendFunctionOrdinal(value), value);
    for (int value = 0; value <= 7; ++value)
    {
        EXPECT_EQ(NormalizeXnaCompareFunctionOrdinal(value), value);
        EXPECT_EQ(NormalizeXnaStencilOperationOrdinal(value), value);
    }
    for (int value = 0; value <= 2; ++value)
    {
        EXPECT_EQ(NormalizeXnaCullModeOrdinal(value), value);
        EXPECT_EQ(NormalizeXnaTextureAddressModeOrdinal(value), value);
    }
    for (int value = 0; value <= 1; ++value) EXPECT_EQ(NormalizeXnaFillModeOrdinal(value), value);
    for (int value = 0; value <= 8; ++value)
        EXPECT_EQ(NormalizeXnaTextureFilterOrdinal(value), value);
}

TEST(StateEnumFallbackTest, InvalidBlendValuesRenderAsZeroAndAddWhileRemainingObservable)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice();
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);

    const Blend invalidBlend = static_cast<Blend>(12345);
    const BlendFunction invalidFunction = static_cast<BlendFunction>(12345);
    BlendState state;
    state.setColorSourceBlendProperty(invalidBlend);
    state.setColorDestinationBlendProperty(invalidBlend);
    state.setAlphaSourceBlendProperty(invalidBlend);
    state.setAlphaDestinationBlendProperty(invalidBlend);
    state.setColorBlendFunctionProperty(invalidFunction);
    state.setAlphaBlendFunctionProperty(invalidFunction);

    ASSERT_NO_THROW(device.setBlendStateProperty(state));
    EXPECT_EQ(device.getBlendStateProperty().getColorSourceBlendProperty(), invalidBlend);
    EXPECT_EQ(device.getBlendStateProperty().getColorBlendFunctionProperty(), invalidFunction);
    device.Clear(Color::Black);
    DrawFullScreen(device, Color::Red, 0.0f);
    EXPECT_EQ(ReadPixel(device, 4, 4), Color::Transparent);
}

TEST(StateEnumFallbackTest, InvalidCullAndFillRenderAsCullNoneAndSolid)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice();
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);

    const CullMode invalidCull = static_cast<CullMode>(12345);
    const FillMode invalidFill = static_cast<FillMode>(12345);
    RasterizerState state;
    state.setCullModeProperty(invalidCull);
    state.setFillModeProperty(invalidFill);
    ASSERT_NO_THROW(device.setRasterizerStateProperty(state));
    EXPECT_EQ(device.getRasterizerStateProperty().getCullModeProperty(), invalidCull);
    EXPECT_EQ(device.getRasterizerStateProperty().getFillModeProperty(), invalidFill);

    device.Clear(Color::Black);
    DrawOppositeWindingTriangles(device, Color::Green);
    EXPECT_EQ(ReadPixel(device, 2, 4), Color::Green);
    EXPECT_EQ(ReadPixel(device, 6, 4), Color::Green);
}

TEST(StateEnumFallbackTest, InvalidDepthComparisonRendersAsAlways)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice(DepthFormat::Depth24);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setBlendStateProperty(BlendState::Opaque);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);

    device.setDepthStencilStateProperty(DepthStencilState::Default);
    DrawFullScreen(device, Color::Red, 0.2f);

    const CompareFunction invalidCompare = static_cast<CompareFunction>(12345);
    DepthStencilState state;
    state.setDepthBufferFunctionProperty(invalidCompare);
    state.setDepthBufferWriteEnableProperty(false);
    ASSERT_NO_THROW(device.setDepthStencilStateProperty(state));
    EXPECT_EQ(device.getDepthStencilStateProperty().getDepthBufferFunctionProperty(),
              invalidCompare);
    DrawFullScreen(device, Color::Green, 0.8f);
    EXPECT_EQ(ReadPixel(device, 4, 4), Color::Green);
}

TEST(StateEnumFallbackTest, InvalidStencilValuesRenderAsAlwaysAndKeepOnBothWindings)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice(DepthFormat::Depth24Stencil8);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setBlendStateProperty(BlendState::Opaque);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                 Color::Black, 1.0f, 0);

    const CompareFunction invalidCompare = static_cast<CompareFunction>(12345);
    const StencilOperation invalidOperation = static_cast<StencilOperation>(12345);
    DepthStencilState state;
    state.setDepthBufferEnableProperty(false);
    state.setDepthBufferWriteEnableProperty(false);
    state.setStencilEnableProperty(true);
    state.setStencilFunctionProperty(invalidCompare);
    state.setStencilPassProperty(invalidOperation);
    state.setStencilFailProperty(invalidOperation);
    state.setStencilDepthBufferFailProperty(invalidOperation);
    state.setReferenceStencilProperty(1);
    state.setTwoSidedStencilModeProperty(true);
    state.setCounterClockwiseStencilFunctionProperty(invalidCompare);
    state.setCounterClockwiseStencilPassProperty(invalidOperation);
    state.setCounterClockwiseStencilFailProperty(invalidOperation);
    state.setCounterClockwiseStencilDepthBufferFailProperty(invalidOperation);
    ASSERT_NO_THROW(device.setDepthStencilStateProperty(state));
    DrawOppositeWindingTriangles(device, Color::Red);
    EXPECT_EQ(ReadPixel(device, 2, 4), Color::Red);
    EXPECT_EQ(ReadPixel(device, 6, 4), Color::Red);

    DepthStencilState requireUnchangedZero;
    requireUnchangedZero.setDepthBufferEnableProperty(false);
    requireUnchangedZero.setDepthBufferWriteEnableProperty(false);
    requireUnchangedZero.setStencilEnableProperty(true);
    requireUnchangedZero.setStencilFunctionProperty(CompareFunction::Equal);
    requireUnchangedZero.setReferenceStencilProperty(0);
    device.setDepthStencilStateProperty(requireUnchangedZero);
    DrawOppositeWindingTriangles(device, Color::Green);
    EXPECT_EQ(ReadPixel(device, 2, 4), Color::Green);
    EXPECT_EQ(ReadPixel(device, 6, 4), Color::Green);
}

TEST(StateEnumFallbackTest, InvalidSpriteSamplerAddressRendersAsWrap)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice();
    Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
    const std::array texels{Color::Red, Color::Green, Color::Blue, Color::White};
    texture.SetData(texels.data(), static_cast<int>(texels.size()));

    const TextureAddressMode invalidAddress = static_cast<TextureAddressMode>(12345);
    SamplerState sampler = SamplerState::PointClamp;
    sampler.setAddressUProperty(invalidAddress);
    sampler.setAddressVProperty(invalidAddress);
    SpriteBatch batch(device);
    device.Clear(Color::Black);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &sampler,
                &DepthStencilState::None, &RasterizerState::CullNone);
    batch.Draw(texture, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
    batch.End();

    EXPECT_EQ(device.getSamplerStatesProperty()[0].getAddressUProperty(), invalidAddress);
    EXPECT_EQ(device.getSamplerStatesProperty()[0].getAddressVProperty(), invalidAddress);
    EXPECT_EQ(ReadPixel(device, 4, 4), Color::Red);
}

TEST(StateEnumFallbackTest, InvalidSpriteSamplerFilterRendersAsLinear)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    GraphicsDevice device = MakeDevice();
    Texture2D texture(device, 2, 1, false, SurfaceFormat::Color);
    const std::array texels{Color::Red, Color::Green};
    texture.SetData(texels.data(), static_cast<int>(texels.size()));

    const TextureFilter invalidFilter = static_cast<TextureFilter>(12345);
    SamplerState sampler = SamplerState::PointClamp;
    sampler.setFilterProperty(invalidFilter);
    SpriteBatch batch(device);
    device.Clear(Color::Black);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &sampler,
                &DepthStencilState::None, &RasterizerState::CullNone);
    batch.Draw(texture, Rectangle(0, 0, 8, 8), Color::White);
    batch.End();

    EXPECT_EQ(device.getSamplerStatesProperty()[0].getFilterProperty(), invalidFilter);
    const Color sample = ReadPixel(device, 3, 4);
    EXPECT_GT(sample.getRProperty(), 0);
    EXPECT_LT(sample.getRProperty(), 255);
    EXPECT_GT(sample.getGProperty(), 0);
    EXPECT_LT(sample.getGProperty(), 255);
    EXPECT_EQ(sample.getBProperty(), 0);
    EXPECT_EQ(sample.getAProperty(), 255);
}
