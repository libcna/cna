// SPDX-License-Identifier: MS-PL
// SOFTWARE-181/SOFTWARE-336..338/SOFTWARE-340/341: selected depth storage, XNA clip-depth
// semantics, complete SpriteBatch transforms and safe signed source geometry affect fragments.

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <utility>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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

    Color RenderClearDepthProbe(float clearDepth, CompareFunction function, float fragmentDepth)
    {
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
            BackbufferParameters(DepthFormat::Depth24));
        DepthStencilState state;
        state.setDepthBufferEnableProperty(true);
        state.setDepthBufferWriteEnableProperty(true);
        state.setDepthBufferFunctionProperty(function);
        PrepareDraw(device, state);

        // Collapse the viewport depth range so both Direct3D-style and OpenGL-style clip-space
        // conventions produce the exact endpoint needed by this clear-value discriminator.
        Viewport viewport = device.getViewportProperty();
        viewport.setMinDepthProperty(fragmentDepth);
        viewport.setMaxDepthProperty(fragmentDepth);
        device.setViewportProperty(viewport);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Black, clearDepth, 0);
        DrawFullScreen(device, Color::Red, 0.5f);
        return ReadCenter(device);
    }

    Color RenderSpriteDepthWinner()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 1, 1, false, SurfaceFormat::Color);
        Texture2D green(device, 1, 1, false, SurfaceFormat::Color);
        const Color redPixel = Color::Red;
        const Color greenPixel = Color::Green;
        red.SetData(&redPixel, 1);
        green.SetData(&greenPixel, 1);

        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Black, 1.0f, 0);

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::Default, &RasterizerState::CullNone);
        // Deferred preserves submission order. XNA/FNA write layerDepth into POSITION.Z, so the
        // nearer red sprite writes depth 0.1 and the farther green sprite must then fail LessEqual.
        // A renderer that treats layerDepth only as a CPU sort key gives both quads equal depth and
        // incorrectly lets the later green sprite overwrite red.
        batch.Draw(red, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        batch.Draw(green, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadCenter(target);
    }

    Color RenderSpriteTransformedDepthProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 1, 1, false, SurfaceFormat::Color);
        const Color redPixel = Color::Red;
        red.SetData(&redPixel, 1);

        DepthStencilState less;
        less.setDepthBufferEnableProperty(true);
        less.setDepthBufferWriteEnableProperty(true);
        less.setDepthBufferFunctionProperty(CompareFunction::Less);

        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Black, 0.25f, 0);

        SpriteBatch batch(device);
        const Matrix transform = Matrix::CreateTranslation(0.0f, 0.0f, 0.5f);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &less, &RasterizerState::CullNone, nullptr, transform);
        batch.Draw(red, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadCenter(target);
    }

    std::pair<Color, Color> RenderSpriteHomogeneousWProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 1, 1, false, SurfaceFormat::Color);
        const Color redPixel = Color::Red;
        red.SetData(&redPixel, 1);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        Matrix transform = Matrix::getIdentityProperty();
        transform.M44 = 2.0f;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone, nullptr, transform);
        batch.Draw(red, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);

        Color inside;
        Color outside;
        const Rectangle insideRect(2, 2, 1, 1);
        const Rectangle outsideRect(6, 6, 1, 1);
        target.GetData(0, &insideRect, &inside, 0, 1);
        target.GetData(0, &outsideRect, &outside, 0, 1);
        return {inside, outside};
    }

    std::pair<Color, Color> RenderSpriteTransformedNearClipProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 1, 1, false, SurfaceFormat::Color);
        const Color redPixel = Color::Red;
        red.SetData(&redPixel, 1);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        // z = x/4 - 1 crosses XNA's homogeneous near plane at sprite x=4 and reaches the
        // far plane at x=8. The left half must be clipped while the right half remains visible.
        Matrix transform = Matrix::getIdentityProperty();
        transform.M13 = 0.25f;
        transform.M43 = -1.0f;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone, nullptr, transform);
        batch.Draw(red, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);

        Color clipped;
        Color visible;
        const Rectangle clippedRect(2, 4, 1, 1);
        const Rectangle visibleRect(6, 4, 1, 1);
        target.GetData(0, &clippedRect, &clipped, 0, 1);
        target.GetData(0, &visibleRect, &visible, 0, 1);
        return {clipped, visible};
    }

    std::pair<Color, Color> RenderSpriteVaryingWProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 2, 1, false, SurfaceFormat::Color);
        const Color pixels[2] = {Color::Red, Color::Green};
        texture.SetData(pixels, 2);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        // w = 1 + x/8 maps the right edge from x=8 to x=4. At screen x=3.5 the
        // perspective-correct source coordinate is about 0.78 (green); affine interpolation of
        // the post-divide endpoints would incorrectly produce about 0.44 (red).
        Matrix transform = Matrix::getIdentityProperty();
        transform.M14 = 0.125f;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone, nullptr, transform);
        batch.Draw(texture, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 2, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);

        Color perspectiveCorrect;
        Color outside;
        const Rectangle perspectiveRect(3, 2, 1, 1);
        const Rectangle outsideRect(5, 2, 1, 1);
        target.GetData(0, &perspectiveRect, &perspectiveCorrect, 0, 1);
        target.GetData(0, &outsideRect, &outside, 0, 1);
        return {perspectiveCorrect, outside};
    }

    Color RenderSpriteViewportDepthRangeProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 1, 1, false, SurfaceFormat::Color);
        const Color redPixel = Color::Red;
        red.SetData(&redPixel, 1);

        DepthStencilState less;
        less.setDepthBufferEnableProperty(true);
        less.setDepthBufferWriteEnableProperty(true);
        less.setDepthBufferFunctionProperty(CompareFunction::Less);

        device.SetRenderTarget(&target);
        Viewport viewport = device.getViewportProperty();
        viewport.setMinDepthProperty(0.4f);
        viewport.setMaxDepthProperty(0.8f);
        device.setViewportProperty(viewport);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                     Color::Black, 0.55f, 0);

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &less, &RasterizerState::CullNone);
        batch.Draw(red, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.5f);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadCenter(target);
    }

    std::pair<Color, Color> RenderSpriteNegativeSourceOriginProbe(bool rectangleDestination)
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 2, 2, false, SurfaceFormat::Color);
        const Color pixels[4] = {Color::Red, Color::Red, Color::Red, Color::Red};
        red.SetData(pixels, 4);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        const Rectangle source(2, 0, -2, 2);
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone);
        if (rectangleDestination)
        {
            batch.Draw(red, Rectangle(2, 2, 4, 4), std::optional<Rectangle>(source),
                       Color::White, 0.0f, Vector2(1.0f, 0.0f), SpriteEffects::None, 0.0f);
        }
        else
        {
            batch.Draw(red, Vector2(6.0f, 2.0f), std::optional<Rectangle>(source),
                       Color::White, 0.0f, Vector2(1.0f, 0.0f), 2.0f,
                       SpriteEffects::None, 0.0f);
        }
        batch.End();
        device.SetRenderTarget(nullptr);

        Color expected;
        Color opposite;
        const Rectangle expectedRect(
            rectangleDestination ? 6 : 2, 3, 1, 1);
        const Rectangle oppositeRect(
            rectangleDestination ? 0 : 7, 3, 1, 1);
        target.GetData(0, &expectedRect, &expected, 0, 1);
        target.GetData(0, &oppositeRect, &opposite, 0, 1);
        return {expected, opposite};
    }

    Color RenderSpriteZeroWidthSourceProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D red(device, 2, 2, false, SurfaceFormat::Color);
        const Color pixels[4] = {Color::Red, Color::Red, Color::Red, Color::Red};
        red.SetData(pixels, 4);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone);
        batch.Draw(red, Rectangle(2, 2, 4, 4),
                   std::optional<Rectangle>(Rectangle(0, 0, 0, 2)), Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        device.SetRenderTarget(nullptr);

        Color center;
        const Rectangle centerRect(3, 3, 1, 1);
        target.GetData(0, &centerRect, &center, 0, 1);
        return center;
    }

    Color RenderSpriteOverflowingSourceEndpointProbe()
    {
        GraphicsDevice device;
        RenderTarget2D target(
            device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        Texture2D texture(device, 2, 2, false, SurfaceFormat::Color);
        const Color pixels[4] = {Color::Red, Color::Green, Color::Blue, Color::Yellow};
        texture.SetData(pixels, 4);

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        const int maximum = std::numeric_limits<int>::max();
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp,
                    &DepthStencilState::None, &RasterizerState::CullNone);
        batch.Draw(texture, Rectangle(0, 0, 8, 8),
                   std::optional<Rectangle>(Rectangle(maximum, maximum, 1, 1)), Color::White);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadCenter(target);
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

    // The missing attachment is still invalid even when the supplied depth would otherwise be
    // saturated by the native clear path.
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

TEST(BackBufferDepthStencilContractTest, ExplicitDepthClearSaturatesLikeMicrosoftXna)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const float infinity = std::numeric_limits<float>::infinity();
    EXPECT_EQ(RenderClearDepthProbe(-0.25f, CompareFunction::Greater, 0.0f), Color::Black);
    EXPECT_EQ(RenderClearDepthProbe(1.25f, CompareFunction::Less, 1.0f), Color::Black);
    EXPECT_EQ(RenderClearDepthProbe(-infinity, CompareFunction::Greater, 0.0f), Color::Black);
    EXPECT_EQ(RenderClearDepthProbe(infinity, CompareFunction::Less, 1.0f), Color::Black);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(RenderClearDepthProbe(nan, CompareFunction::Greater, 0.5f), Color::Red);
    EXPECT_EQ(RenderClearDepthProbe(nan, CompareFunction::Less, 0.5f), Color::Black);
}

TEST(BackBufferDepthStencilContractTest, StockEffectUsesXnaClipDepthConvention)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        BackbufferParameters(DepthFormat::Depth24));

    DepthStencilState equal;
    equal.setDepthBufferEnableProperty(true);
    equal.setDepthBufferWriteEnableProperty(true);
    equal.setDepthBufferFunctionProperty(CompareFunction::Equal);
    PrepareDraw(device, equal);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                 Color::Black, 0.0f, 0);
    DrawFullScreen(device, Color::Red, 0.0f);
    EXPECT_EQ(ReadCenter(device), Color::Red)
        << "XNA clip z=0 is the near depth endpoint, not OpenGL window depth 0.5";

    PrepareDraw(device, DepthStencilState::None);
    device.Clear(Color::Black);
    DrawFullScreen(device, Color::Red, -0.5f);
    EXPECT_EQ(ReadCenter(device), Color::Black)
        << "XNA clips stock-effect geometry with clip z below zero";
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchLayerDepthParticipatesInDepthTesting)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderSpriteDepthWinner(), Color::Red);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchTransformsLayerDepthBeforeDepthTesting)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderSpriteTransformedDepthProbe(), Color::Black)
        << "SpriteBatch must transform POSITION.Z before the depth test";
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchPreservesHomogeneousTransformW)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const auto [inside, outside] = RenderSpriteHomogeneousWProbe();
    EXPECT_EQ(inside, Color::Red);
    EXPECT_EQ(outside, Color::Black)
        << "M44=2 must divide the sprite's X/Y extent by homogeneous W";
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchClipsTransformedDepthAtXnaNearPlane)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const auto [clipped, visible] = RenderSpriteTransformedNearClipProbe();
    EXPECT_EQ(clipped, Color::Black);
    EXPECT_EQ(visible, Color::Red);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchInterpolatesThroughVaryingTransformW)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const auto [perspectiveCorrect, outside] = RenderSpriteVaryingWProbe();
    EXPECT_EQ(perspectiveCorrect, Color::Green);
    EXPECT_EQ(outside, Color::Black);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchUsesViewportDepthRange)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderSpriteViewportDepthRangeProbe(), Color::Black)
        << "layer depth 0.5 must map to 0.6 through viewport depth range [0.4, 0.8]";
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchVectorDrawPreservesNegativeSourceOrigin)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const auto [expected, opposite] = RenderSpriteNegativeSourceOriginProbe(false);
    EXPECT_EQ(expected, Color::Red);
    EXPECT_EQ(opposite, Color::Black);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchRectangleDrawPreservesNegativeSourceOrigin)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    const auto [expected, opposite] = RenderSpriteNegativeSourceOriginProbe(true);
    EXPECT_EQ(expected, Color::Red);
    EXPECT_EQ(opposite, Color::Black);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchZeroWidthSourceDoesNotInventGeometry)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderSpriteZeroWidthSourceProbe(), Color::Black);
}

TEST(BackBufferDepthStencilContractTest, SpriteBatchSourceEndpointsUseFloatDomainWithoutOverflow)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    EXPECT_EQ(RenderSpriteOverflowingSourceEndpointProbe(), Color::Yellow);
}

TEST(BackBufferDepthStencilContractTest, UnknownClearOptionBitsAreIgnored)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
        BackbufferParameters(DepthFormat::Depth24));
    device.Clear(Color::Red);

    EXPECT_NO_THROW(device.Clear(static_cast<ClearOptions>(8), Color::Blue,
                                 std::numeric_limits<float>::quiet_NaN(), 0));
    EXPECT_EQ(ReadCenter(device), Color::Red);

    const auto targetAndUnknown = static_cast<ClearOptions>(
        static_cast<int>(ClearOptions::Target) | 8);
    EXPECT_NO_THROW(device.Clear(targetAndUnknown, Color::Blue, -99.0f, 0));
    EXPECT_EQ(ReadCenter(device), Color::Blue);
}
