// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

TEST(SpriteBatchBoundaryTest, RejectedBeginPreservesQueuedWorkAndOriginalMode)
{
    // XNA Begin(7 args) IL_0000-0012 rejects before touching mode/state/queue;
    // End IL_0000-0012 rejects a closed pair, IL_0038-0049 drains once then closes.
    // InternalDraw IL_01d0-0232 dispatches only Immediate before End.
    for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate,
                           SpriteSortMode::Texture, SpriteSortMode::BackToFront,
                           SpriteSortMode::FrontToBack})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        auto* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        const auto texture = Texture2D::CreateWithRendererForTests(
            32,16,std::make_shared<DummyTextureRenderer>(32,16));
        EXPECT_THROW(batch.End(),System::InvalidOperationException);
        EXPECT_EQ(rec->endCount,0);
        batch.Begin(mode,BlendState::AlphaBlend);
        batch.Draw(texture,Vector2(7.5f,11.25f),Color(13,29,47,255));
        const auto rejectedMode = mode == SpriteSortMode::Immediate
            ? SpriteSortMode::Deferred : SpriteSortMode::Immediate;
        EXPECT_THROW(batch.Begin(rejectedMode,BlendState::Opaque),System::InvalidOperationException);
        EXPECT_EQ(rec->beginCount,1);
        EXPECT_EQ(rec->drawCalls.size(),mode == SpriteSortMode::Immediate ? 1u : 0u);
        batch.End();
        ASSERT_EQ(rec->drawCalls.size(),1u);
        EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationX,7.5f);
        EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationY,11.25f);
        EXPECT_EQ(rec->drawCalls[0].color,Color(13,29,47,255));
        EXPECT_EQ(rec->drawCalls[0].texture,texture.GetRendererWeak().lock().get());
        EXPECT_EQ(rec->endCount,1);
        EXPECT_THROW(batch.End(),System::InvalidOperationException);
        EXPECT_EQ(rec->endCount,1);
        batch.Begin(mode,BlendState::AlphaBlend);
        batch.End();
        EXPECT_EQ(rec->beginCount,2);
        EXPECT_EQ(rec->endCount,2);
        EXPECT_EQ(rec->drawCalls.size(),1u); // Empty next batch must not replay old work.
        batch.Begin(mode,BlendState::AlphaBlend);
        batch.Draw(texture,Vector2(23,31),Color::White);
        batch.End();
        ASSERT_EQ(rec->drawCalls.size(),2u);
        EXPECT_FLOAT_EQ(rec->drawCalls.back().destinationX,23);
        EXPECT_FLOAT_EQ(rec->drawCalls.back().destinationY,31);
    }
}

TEST(SpriteBatchBoundaryTest, PositionOverloadsResolveNullSourceAndScaleAxes)
{
    // XNA SpriteBatch.InternalDraw IL_006b-0144 selects source or whole texture,
    // multiplies each source dimension by its scale; IL_0183-01c8 copies other fields.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    const auto texture = Texture2D::CreateWithRendererForTests(
        32,16,std::make_shared<DummyTextureRenderer>(32,16));
    const Color tint(11,23,47,89);
    const Vector2 position(7.5f,11.25f), origin(1.5f,2.5f);
    batch.Begin();
    batch.Draw(texture,position,tint);
    for (const bool explicitSource : {false,true})
    {
        const std::optional<Rectangle> source = explicitSource
            ? std::optional<Rectangle>(Rectangle(4,6,8,4)) : std::nullopt;
        batch.Draw(texture,position,source,tint);
        batch.Draw(texture,position,source,tint,0.25f,origin,2.0f,
                   SpriteEffects::FlipVertically,0.625f);
        batch.Draw(texture,position,source,tint,0.25f,origin,Vector2(2,3),
                   SpriteEffects::FlipVertically,0.625f);
    }
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(),7u);
    const float widths[] = {32,32,64,64,8,16,16};
    const float heights[] = {16,16,32,48,4,8,12};
    for (int i=0;i<7;++i)
    {
        SCOPED_TRACE(i);
        const auto& call=rec->drawCalls[i];
        const bool transformed = i==2 || i==3 || i==5 || i==6;
        EXPECT_EQ(call.texture,texture.GetRendererWeak().lock().get());
        EXPECT_FLOAT_EQ(call.destinationX,7.5f);
        EXPECT_FLOAT_EQ(call.destinationY,11.25f);
        EXPECT_FLOAT_EQ(call.destinationWidth,widths[i]);
        EXPECT_FLOAT_EQ(call.destinationHeight,heights[i]);
        EXPECT_EQ(call.sourceRectangle,i<4 ? Rectangle(0,0,32,16) : Rectangle(4,6,8,4));
        EXPECT_EQ(call.color,tint);
        EXPECT_EQ(call.origin,transformed ? origin : Vector2::Zero);
        EXPECT_FLOAT_EQ(call.rotation,transformed ? 0.25f : 0);
        EXPECT_EQ(call.effects,transformed ? SpriteEffects::FlipVertically : SpriteEffects::None);
        EXPECT_FLOAT_EQ(call.layerDepth,transformed ? 0.625f : 0);
    }
}
