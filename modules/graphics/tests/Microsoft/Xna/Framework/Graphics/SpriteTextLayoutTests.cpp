// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/Text/StringBuilder.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

namespace
{
    SpriteFont LayoutFont(std::optional<SharpRuntime::charcs> fallback = std::nullopt)
    {
        auto atlas = Texture2D::CreateWithRendererForTests(
            32, 32, std::make_shared<DummyTextureRenderer>(32, 32));
        return SpriteFont(atlas, {{2, 4, 6, 8}, {12, 3, 4, 5}},
                          {{2, 3, 10, 11}, {-1, 1, 7, 13}}, {u'A', u'B'},
                          12, 2.0f, {Vector3(-2, 7, -1), Vector3(3, 5, 2)}, fallback);
    }

    void ExpectGlyph(const RecordingSpriteBatchRenderer::DrawCall& call,
                     const Rectangle& source, float x, float y, float sx, float sy,
                     const Color& color, float rotation, SpriteEffects effects, float depth)
    {
        EXPECT_NE(call.texture, nullptr);
        EXPECT_EQ(call.sourceRectangle, source);
        EXPECT_NEAR(call.destinationX, x, 0.0001f);
        EXPECT_NEAR(call.destinationY, y, 0.0001f);
        EXPECT_FLOAT_EQ(call.destinationWidth, source.Width * sx);
        EXPECT_FLOAT_EQ(call.destinationHeight, source.Height * sy);
        EXPECT_EQ(call.color, color);
        EXPECT_FLOAT_EQ(call.rotation, rotation);
        EXPECT_EQ(call.origin, Vector2::Zero);
        EXPECT_EQ(call.effects, effects);
        EXPECT_FLOAT_EQ(call.layerDepth, depth);
    }
}

TEST(SpriteTextLayoutTest, EveryOverloadUsesIndependentMultilineCoordinates)
{
    // XNA SpriteFont.InternalDraw IL_00c4-0199, 01fb-029b: CR ignored, newline resets
    // first bearing, crop is added, then scale/origin/rotation. For A\rB\nBA the
    // unscaled crop-adjusted positions are (2,3),(10,1),(2,13),(12,15).
    const SpriteFont font = LayoutFont();
    const Rectangle sources[] = {{2,4,6,8}, {12,3,4,5}, {12,3,4,5}, {2,4,6,8}};
    const Vector2 expected[3][4] = {
        {{22,33}, {30,31}, {22,43}, {32,45}},
        {{22,32}, {38,28}, {22,52}, {42,56}},
        {{17,32}, {23,48}, {-13,32}, {-19,52}}
    };
    const Color tint(19, 37, 59, 83);
    const float halfPi = 1.5707963267948966f;
    for (int family = 0; family < 6; ++family)
    {
        SCOPED_TRACE(family);
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        auto* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        const std::string text("A\rB\nBA");
        System::Text::StringBuilder builder;
        builder.Append("A\rB").Append('\n').Append("BA");
        batch.Begin();
        switch (family)
        {
            case 0: batch.DrawString(font, text, Vector2(20,30), tint); break;
            case 1: batch.DrawString(font, text, Vector2(20,30), tint, 0, Vector2(1,2),
                                    2.0f, SpriteEffects::None, 0.625f); break;
            case 2: batch.DrawString(font, text, Vector2(20,30), tint, halfPi, Vector2(1,2),
                                    Vector2(2,3), SpriteEffects::None, 0.625f); break;
            case 3: batch.DrawString(font, builder, Vector2(20,30), tint); break;
            case 4: batch.DrawString(font, builder, Vector2(20,30), tint, 0, Vector2(1,2),
                                    2.0f, SpriteEffects::None, 0.625f); break;
            case 5: batch.DrawString(font, builder, Vector2(20,30), tint, halfPi, Vector2(1,2),
                                    Vector2(2,3), SpriteEffects::None, 0.625f); break;
        }
        EXPECT_TRUE(rec->drawCalls.empty());
        batch.End();
        ASSERT_EQ(rec->drawCalls.size(), 4u);
        const int kind = family % 3;
        for (int i = 0; i < 4; ++i)
            ExpectGlyph(rec->drawCalls[i], sources[i], expected[kind][i].X,
                        expected[kind][i].Y, kind == 0 ? 1 : 2, kind == 0 ? 1 : kind == 1 ? 2 : 3,
                        tint, kind == 2 ? halfPi : 0, SpriteEffects::None, kind == 0 ? 0 : 0.625f);
    }
}

TEST(SpriteTextLayoutTest, EmptyAndControlOnlyTextSubmitNoGlyphs)
{
    // XNA InternalDraw IL_00b6, 00c4-0125, 02a4: no Draw for empty, CR or LF.
    for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
    {
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        auto* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        const SpriteFont font = LayoutFont();
        batch.Begin(mode, BlendState::AlphaBlend);
        batch.DrawString(font, std::string(""), Vector2::Zero, Color::White);
        batch.DrawString(font, System::Text::StringBuilder("\r\n\n\r"), Vector2::Zero, Color::White);
        EXPECT_TRUE(rec->drawCalls.empty());
        batch.End();
        EXPECT_TRUE(rec->drawCalls.empty());
        EXPECT_EQ(rec->endCount, 1);
    }
}

TEST(SpriteTextLayoutTest, MissingCharacterUsesFallbackGlyphAndItsMetrics)
{
    // XNA GetIndexForCharacter IL_0043-0069; InternalDraw IL_012a-029b.
    // "?A", fallback B: B is first at (3-1,1); A follows at (3+5+2+2-2+2,3).
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    const SpriteFont font = LayoutFont(u'B');
    batch.Begin();
    batch.DrawString(font, std::string("?A"), Vector2(20,30), Color::White);
    batch.DrawString(font, System::Text::StringBuilder("?A"), Vector2(20,30), Color::White);
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(), 4u);
    for (int start : {0, 2})
    {
        ExpectGlyph(rec->drawCalls[start], Rectangle(12,3,4,5), 22,31,1,1,
                    Color::White,0,SpriteEffects::None,0);
        ExpectGlyph(rec->drawCalls[start+1], Rectangle(2,4,6,8),32,33,1,1,
                    Color::White,0,SpriteEffects::None,0);
    }
}

TEST(SpriteTextLayoutTest, MissingCharacterPreservesPrefixUnlessFlipPremeasures)
{
    // XNA InternalDraw IL_0054-0098 premeasures either flip before drawing; without
    // flips IL_012a-0270 draws A before '?' throws. GetIndexForCharacter IL_0095-00a1:
    // ArgumentException, ParamName="character". A later valid Draw still works.
    for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
    for (const auto effects : {SpriteEffects::None, SpriteEffects::FlipHorizontally,
                              SpriteEffects::FlipVertically})
    for (const bool builder : {false, true})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(static_cast<int>(effects));
        SCOPED_TRACE(builder);
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        auto* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        const SpriteFont font = LayoutFont();
        batch.Begin(mode, BlendState::AlphaBlend);
        try
        {
            if (builder)
                batch.DrawString(font, System::Text::StringBuilder("A?"), Vector2::Zero,
                                 Color::White, 0, Vector2::Zero, 1.0f, effects, 0);
            else
                batch.DrawString(font, std::string("A?"), Vector2::Zero,
                                 Color::White, 0, Vector2::Zero, 1.0f, effects, 0);
            FAIL() << "Expected missing-character refusal";
        }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "character");
        }
        catch (...) { FAIL() << "Wrong exception type"; }
        const std::size_t prefix = effects == SpriteEffects::None ? 1u : 0u;
        EXPECT_EQ(rec->drawCalls.size(), mode == SpriteSortMode::Immediate ? prefix : 0u);
        batch.DrawString(font, std::string("B"), Vector2(40,50), Color::White);
        batch.End();
        ASSERT_EQ(rec->drawCalls.size(), prefix + 1u);
        if (prefix)
            ExpectGlyph(rec->drawCalls[0],Rectangle(2,4,6,8),2,3,1,1,
                        Color::White,0,SpriteEffects::None,0);
        ExpectGlyph(rec->drawCalls.back(),Rectangle(12,3,4,5),42,51,1,1,
                    Color::White,0,SpriteEffects::None,0);
    }
}
