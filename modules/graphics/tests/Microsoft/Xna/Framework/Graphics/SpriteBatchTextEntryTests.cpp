// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

class SpriteBatchTextEntryTest : public testing::TestWithParam<int>
{
protected:
    std::unique_ptr<RecordingSpriteBatchRenderer> renderer =
        std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch{std::move(renderer)};
    SpriteFont font{Texture2D::CreateWithRendererForTests(
        8, 8, std::make_shared<DummyTextureRenderer>(8, 8)),
        {Rectangle(0, 0, 4, 6)}, {Rectangle(0, 0, 4, 6)}, {u'A'}, 8, 0.0f,
        {Vector3(0, 4, 0)}, std::nullopt};

    void Draw(const std::string& text, SpriteEffects effects = SpriteEffects::None)
    {
        System::Text::StringBuilder builder;
        builder.Append(text);
        switch (GetParam())
        {
        case 0: batch.DrawString(font, text, Vector2(10, 20), Color::White); break;
        case 1: batch.DrawString(font, text, Vector2(10, 20), Color::White,
                    0, Vector2::Zero, 1.0f, effects, 0); break;
        case 2: batch.DrawString(font, text, Vector2(10, 20), Color::White,
                    0, Vector2::Zero, Vector2::One, effects, 0); break;
        case 3: batch.DrawString(font, builder, Vector2(10, 20), Color::White); break;
        case 4: batch.DrawString(font, builder, Vector2(10, 20), Color::White,
                    0, Vector2::Zero, 1.0f, effects, 0); break;
        case 5: batch.DrawString(font, builder, Vector2(10, 20), Color::White,
                    0, Vector2::Zero, Vector2::One, effects, 0); break;
        }
    }
};

TEST_P(SpriteBatchTextEntryTest, EmptyAndControlOnlyTextDoNotOpenOrRequireBegin)
{
    // Refresh SpriteFont.InternalDraw IL_00b6, 00c4-0125, 02a4-02b0: no glyph,
    // no SpriteBatch.Draw (IL_0270), hence no Begin guard. FNA CheckBegin differs.
    for (const auto* text : {"", "\r", "\n", "\r\n\n"})
    {
        SCOPED_TRACE(text);
        EXPECT_NO_THROW(Draw(text));
        EXPECT_EQ(rec->beginCount, 0);
        EXPECT_TRUE(rec->drawCalls.empty());
        EXPECT_THROW(batch.End(), System::InvalidOperationException);
    }
    batch.Begin();
    Draw("A");
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    // Zero bearings/crop/origin: (10,20), glyph dimensions (4,6), default scale 1.
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationX, 10);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationY, 20);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationWidth, 4);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationHeight, 6);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].rotation, 0);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].layerDepth, 0);
    EXPECT_EQ(rec->drawCalls[0].origin, Vector2::Zero);
    EXPECT_EQ(rec->drawCalls[0].effects, SpriteEffects::None);
}

TEST_P(SpriteBatchTextEntryTest, MissingGlyphPrecedesBeginButValidGlyphRequiresBegin)
{
    // Refresh InternalDraw resolves GetIndexForCharacter at IL_012a-0132 before
    // Draw at IL_0270; that Draw reaches SpriteBatch.InternalDraw IL_0013-0025.
    for (const auto effects : {SpriteEffects::None,
                              SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically})
    {
        try
        {
            Draw("?", effects);
            FAIL() << "Missing glyph must fail before Begin validation";
        }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), "character");
        }
        EXPECT_THROW(Draw("A", effects), System::InvalidOperationException);
    }
    EXPECT_TRUE(rec->drawCalls.empty());
    EXPECT_EQ(rec->beginCount, 0);
    batch.Begin();
    Draw("A");
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_EQ(rec->drawCalls[0].sourceRectangle, Rectangle(0, 0, 4, 6));
}

TEST_P(SpriteBatchTextEntryTest, EmptyTextAfterDisposeStillDoesNotRequireBegin)
{
    // Refresh SpriteBatch.DrawString has only font/text null checks; InternalDraw's
    // empty loop returns without touching the disposed batch's native storage.
    batch.Dispose();
    EXPECT_TRUE(batch.getIsDisposedProperty());
    EXPECT_NO_THROW(Draw(""));
    EXPECT_NO_THROW(Draw("\r\n"));
    EXPECT_THROW(Draw("A"), System::InvalidOperationException);
    EXPECT_THROW(batch.End(), System::InvalidOperationException);
}

INSTANTIATE_TEST_SUITE_P(AllSixOverloads, SpriteBatchTextEntryTest, testing::Range(0, 6));
}
