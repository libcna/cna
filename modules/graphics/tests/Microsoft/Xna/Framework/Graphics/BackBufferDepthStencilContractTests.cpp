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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/InvalidOperationException.hpp"

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

    Color ReadCenter(RenderTarget2D& target)
    {
        const Rectangle center(4, 4, 1, 1);
        Color pixel;
        target.GetData(0, &center, &pixel, 0, 1);
        return pixel;
    }

    Color RenderDepthWinner(DepthFormat format)
    {
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            BackbufferParameters(format));
        PrepareDraw(device, DepthStencilState::Default);
        const ClearOptions options = format == DepthFormat::None
            ? ClearOptions::Target
            : ClearOptions::Target | ClearOptions::DepthBuffer;
        device.Clear(options, Color::Black, 1.0f, 0);
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
        const ClearOptions options = format == DepthFormat::Depth24Stencil8
            ? ClearOptions::Target | ClearOptions::Stencil
            : ClearOptions::Target;
        device.Clear(options, Color::Black, 1.0f, 0);
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

TEST(BackBufferDepthStencilContractTest, ExplicitMissingDepthOrStencilThrowsAtomically)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        BackbufferParameters(DepthFormat::None));
    device.Clear(Color::Red);

    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Blue, 1.0f, 0),
        System::InvalidOperationException);
    EXPECT_EQ(ReadCenter(device), Color::Red);

    // Recovered Microsoft code classifies the absent attachment before any other native clear
    // failure; an invalid depth value therefore cannot replace the missing-buffer exception.
    EXPECT_THROW(
        device.Clear(ClearOptions::DepthBuffer, Color::Blue, -1.0f, 0),
        System::InvalidOperationException);
    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::Stencil,
                     Color::Blue, 1.0f, 1),
        System::InvalidOperationException);
    EXPECT_EQ(ReadCenter(device), Color::Red);
}

TEST(BackBufferDepthStencilContractTest, DepthOnlySurfaceAllowsDepthButRejectsStencilAtomically)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        BackbufferParameters(DepthFormat::Depth24));
    device.Clear(Color::Red);

    EXPECT_NO_THROW(
        device.Clear(ClearOptions::DepthBuffer, Color::Blue, 0.25f, 0));
    EXPECT_EQ(ReadCenter(device), Color::Red);

    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::Stencil,
                     Color::Blue, 1.0f, 1),
        System::InvalidOperationException);
    EXPECT_EQ(ReadCenter(device), Color::Red);
}

TEST(BackBufferDepthStencilContractTest, ExplicitMissingRenderTargetAttachmentsThrowAtomically)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device;
    RenderTarget2D colorOnly(
        device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
        RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&colorOnly);
    device.Clear(Color::Red);

    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Blue, 1.0f, 0),
        System::InvalidOperationException);
    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::Stencil,
                     Color::Blue, 1.0f, 1),
        System::InvalidOperationException);
    device.SetRenderTarget(nullptr);
    EXPECT_EQ(ReadCenter(colorOnly), Color::Red);

    RenderTarget2D depthOnly(
        device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
        RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&depthOnly);
    device.Clear(Color::Red);
    EXPECT_NO_THROW(
        device.Clear(ClearOptions::DepthBuffer, Color::Blue, 0.25f, 0));
    EXPECT_THROW(
        device.Clear(ClearOptions::Target | ClearOptions::Stencil,
                     Color::Blue, 1.0f, 1),
        System::InvalidOperationException);
    device.SetRenderTarget(nullptr);
    EXPECT_EQ(ReadCenter(depthOnly), Color::Red);
}

TEST(BackBufferDepthStencilContractTest, SingleArgumentClearUsesOneInsteadOfViewportMaxDepth)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        BackbufferParameters(DepthFormat::Depth24));
    DepthStencilState less;
    less.setDepthBufferEnableProperty(true);
    less.setDepthBufferWriteEnableProperty(true);
    less.setDepthBufferFunctionProperty(CompareFunction::Less);
    PrepareDraw(device, less);

    Viewport viewport = device.getViewportProperty();
    viewport.setMaxDepthProperty(0.25f);
    device.setViewportProperty(viewport);

    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                 Color::Black, viewport.getMaxDepthProperty(), 0);
    DrawFullScreen(device, Color::Red, 1.0f);
    EXPECT_EQ(ReadCenter(device), Color::Black);

    device.Clear(Color::Blue);
    DrawFullScreen(device, Color::Red, 1.0f);
    EXPECT_EQ(ReadCenter(device), Color::Red);
}
