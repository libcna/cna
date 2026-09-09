// SPDX-License-Identifier: MS-PL
// SOFTWARE-181: selected backbuffer depth/stencil storage must affect fragment acceptance.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters BackbufferParameters(DepthFormat depthFormat, int samples = 0)
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(16);
        parameters.setBackBufferHeightProperty(16);
        parameters.setDepthStencilFormatProperty(depthFormat);
        parameters.setMultiSampleCountProperty(samples);
        return parameters;
    }

    void PrepareDraw(GraphicsDevice& device, const DepthStencilState& depthStencilState)
    {
        RasterizerState rasterizer;
        rasterizer.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rasterizer);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(depthStencilState);
    }

    void DrawFullScreen(GraphicsDevice& device, const Color& color, float depth)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();

        const VertexPositionColor vertices[6] = {
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const Rectangle center(8, 8, 1, 1);
        Color pixel;
        device.GetBackBufferData(&center, &pixel, 0, 1);
        return pixel;
    }

    Color RenderDepthWinner(DepthFormat format)
    {
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            BackbufferParameters(format));
        PrepareDraw(device, DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        DrawFullScreen(device, Color::Red, 0.2f);
        DrawFullScreen(device, Color::Green, 0.8f);
        return ReadCenter(device);
    }

    Color RenderStencilProbe(DepthFormat format, int samples)
    {
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            BackbufferParameters(format, samples));

        DepthStencilState requireOne;
        requireOne.setDepthBufferEnableProperty(false);
        requireOne.setDepthBufferWriteEnableProperty(false);
        requireOne.setStencilEnableProperty(true);
        requireOne.setStencilFunctionProperty(CompareFunction::Equal);
        requireOne.setReferenceStencilProperty(1);
        requireOne.setStencilPassProperty(StencilOperation::Keep);
        PrepareDraw(device, requireOne);
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        DrawFullScreen(device, Color::Green, 0.5f);
        return ReadCenter(device);
    }
}

TEST(BackBufferDepthStencilContractTest, NoneAndDepth24SelectDepthFragmentAcceptance)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderDepthWinner(DepthFormat::None), Color::Green);
    EXPECT_EQ(RenderDepthWinner(DepthFormat::Depth24), Color::Red);
}

TEST(BackBufferDepthStencilContractTest, SingleSampleDepth24DoesNotExposeHiddenStencil)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderStencilProbe(DepthFormat::Depth24, 0), Color::Green);
}

TEST(BackBufferDepthStencilContractTest, MultisampleDepth24Stencil8OwnsStencilSamples)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderStencilProbe(DepthFormat::Depth24Stencil8, 4), Color::Black);
}
