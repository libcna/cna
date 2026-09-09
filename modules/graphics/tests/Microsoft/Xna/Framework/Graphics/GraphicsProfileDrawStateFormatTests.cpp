// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/NotSupportedException.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters SmallBackBuffer()
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(8);
        parameters.setBackBufferHeightProperty(8);
        parameters.setDepthStencilFormatProperty(DepthFormat::None);
        return parameters;
    }

    class HiDefDraw
    {
    public:
        HiDefDraw()
            : device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                     SmallBackBuffer())
            , effect(device)
        {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            effect.setLightingEnabledProperty(false);
            effect.setTextureEnabledProperty(false);
            effect.Apply();
        }

        void Draw()
        {
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, vertices.data(), 0, 1);
        }

        GraphicsDevice device;
        BasicEffect effect;
        std::array<VertexPositionTexture, 3> vertices = {
            VertexPositionTexture(Vector3(-0.75f, -0.75f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3( 0.00f,  0.75f, 0.0f), Vector2(0.5f, 0.0f)),
            VertexPositionTexture(Vector3( 0.75f, -0.75f, 0.0f), Vector2(1.0f, 1.0f)),
        };
    };
}

TEST(GraphicsProfileDrawStateFormatTest, FloatAndHalfTexturesRequirePurePointFiltering)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    constexpr std::array restrictedFormats = {
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
    };

    for (const SurfaceFormat format : restrictedFormats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        Texture2D texture(draw.device, 2, 2, false, format);
        draw.effect.setTextureProperty(&texture);
        draw.effect.setTextureEnabledProperty(true);
        draw.effect.Apply();

        draw.device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        EXPECT_NO_THROW(draw.Draw());

        draw.device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
        EXPECT_THROW(draw.Draw(), System::NotSupportedException);
    }
}

TEST(GraphicsProfileDrawStateFormatTest, EveryMixedFilterIsRejectedForRestrictedTextures)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    Texture2D texture(draw.device, 2, 2, false, SurfaceFormat::Single);
    draw.effect.setTextureProperty(&texture);
    draw.effect.setTextureEnabledProperty(true);
    draw.effect.Apply();
    constexpr std::array mixedFilters = {
        TextureFilter::Linear,
        TextureFilter::Anisotropic,
        TextureFilter::LinearMipPoint,
        TextureFilter::PointMipLinear,
        TextureFilter::MinLinearMagPointMipLinear,
        TextureFilter::MinLinearMagPointMipPoint,
        TextureFilter::MinPointMagLinearMipLinear,
        TextureFilter::MinPointMagLinearMipPoint,
    };

    for (const TextureFilter filter : mixedFilters)
    {
        SCOPED_TRACE(static_cast<int>(filter));
        SamplerState state = SamplerState::PointClamp;
        state.setFilterProperty(filter);
        draw.device.getSamplerStatesProperty()[0] = state;
        EXPECT_THROW(draw.Draw(), System::NotSupportedException);
    }
}

TEST(GraphicsProfileDrawStateFormatTest, NonBlendableTargetsRejectBlendAndAllColorMasks)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    constexpr std::array restrictedFormats = {
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
    };

    for (const SurfaceFormat format : restrictedFormats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        RenderTarget2D target(draw.device, 8, 8, false, format, DepthFormat::None);
        draw.device.SetRenderTarget(&target);

        draw.device.setBlendStateProperty(BlendState::Opaque);
        EXPECT_NO_THROW(draw.Draw());

        draw.device.setBlendStateProperty(BlendState::AlphaBlend);
        EXPECT_THROW(draw.Draw(), System::NotSupportedException);

        BlendState masked = BlendState::Opaque;
        masked.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        draw.device.setBlendStateProperty(masked);
        EXPECT_THROW(draw.Draw(), System::NotSupportedException);

        BlendState maskedUnusedSlot = BlendState::Opaque;
        maskedUnusedSlot.setColorWriteChannels1Property(ColorWriteChannels::Red);
        draw.device.setBlendStateProperty(maskedUnusedSlot);
        EXPECT_THROW(draw.Draw(), System::NotSupportedException);
    }
}

TEST(GraphicsProfileDrawStateFormatTest, HdrBlendableTargetAllowsBlending)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    RenderTarget2D target(
        draw.device, 8, 8, false, SurfaceFormat::HdrBlendable, DepthFormat::None);
    draw.device.SetRenderTarget(&target);
    draw.device.setBlendStateProperty(BlendState::AlphaBlend);

    EXPECT_NO_THROW(draw.Draw());
}

TEST(GraphicsProfileDrawStateFormatTest, SpriteBatchEnforcesFilteringAtItsRealFlushTime)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    Texture2D texture(draw.device, 2, 2, false, SurfaceFormat::Single);
    SpriteBatch batch(draw.device);

    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &SamplerState::PointClamp,
                nullptr, nullptr);
    EXPECT_NO_THROW(batch.Draw(texture, Vector2::Zero, Color::White));
    batch.End();

    batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, &SamplerState::LinearClamp,
                nullptr, nullptr);
    EXPECT_THROW(batch.Draw(texture, Vector2::Zero, Color::White),
                 System::NotSupportedException);
    batch.End();

    batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::LinearClamp,
                nullptr, nullptr);
    EXPECT_NO_THROW(batch.Draw(texture, Vector2::Zero, Color::White));
    EXPECT_THROW(batch.End(), System::NotSupportedException);
}

TEST(GraphicsProfileDrawStateFormatTest, SpriteBatchRejectsBlendOnNonBlendableTarget)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    HiDefDraw draw;
    Texture2D texture(draw.device, 1, 1, false, SurfaceFormat::Color);
    RenderTarget2D target(
        draw.device, 8, 8, false, SurfaceFormat::Single, DepthFormat::None);
    draw.device.SetRenderTarget(&target);
    SpriteBatch batch(draw.device);
    batch.Begin(SpriteSortMode::Immediate, &BlendState::AlphaBlend, &SamplerState::PointClamp,
                nullptr, nullptr);

    EXPECT_THROW(batch.Draw(texture, Vector2::Zero, Color::White),
                 System::NotSupportedException);
    batch.End();
}
