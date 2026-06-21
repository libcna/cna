// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include <optional>

#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;

// -----------------------------------------------------------------------
// SpriteSortMode — enum values (XNA 4.0 specifies the underlying integers)
// -----------------------------------------------------------------------

TEST(SpriteSortModeTest, DeferredIsZero)
{
    EXPECT_EQ(static_cast<int>(SpriteSortMode::Deferred), 0);
}

TEST(SpriteSortModeTest, ImmediateIsOne)
{
    EXPECT_EQ(static_cast<int>(SpriteSortMode::Immediate), 1);
}

TEST(SpriteSortModeTest, TextureIsTwo)
{
    EXPECT_EQ(static_cast<int>(SpriteSortMode::Texture), 2);
}

TEST(SpriteSortModeTest, BackToFrontIsThree)
{
    EXPECT_EQ(static_cast<int>(SpriteSortMode::BackToFront), 3);
}

TEST(SpriteSortModeTest, FrontToBackIsFour)
{
    EXPECT_EQ(static_cast<int>(SpriteSortMode::FrontToBack), 4);
}

// -----------------------------------------------------------------------
// SpriteEffects — enum values
// -----------------------------------------------------------------------

TEST(SpriteEffectsTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<int>(SpriteEffects::None), 0);
}

TEST(SpriteEffectsTest, FlipHorizontallyIsOne)
{
    EXPECT_EQ(static_cast<int>(SpriteEffects::FlipHorizontally), 1);
}

TEST(SpriteEffectsTest, FlipVerticallyIsTwo)
{
    EXPECT_EQ(static_cast<int>(SpriteEffects::FlipVertically), 2);
}

TEST(SpriteEffectsTest, FlipHorizontallyAndFlipVerticallyAreDifferent)
{
    EXPECT_NE(SpriteEffects::FlipHorizontally, SpriteEffects::FlipVertically);
}

TEST(SpriteEffectsTest, NoneIsDifferentFromFlipHorizontally)
{
    EXPECT_NE(SpriteEffects::None, SpriteEffects::FlipHorizontally);
}

// -----------------------------------------------------------------------
// SpriteBatch — no-backend construction and guard throws
//
// A default-constructed SpriteBatch has no backend and no graphics device.
// Begin/End/Draw overloads that guard on `begun` still fire before any
// backend or texture access, so these tests do not need a GPU context.
// -----------------------------------------------------------------------

TEST(SpriteBatchTest, DefaultConstructorDoesNotThrow)
{
    EXPECT_NO_THROW(SpriteBatch batch);
}

TEST(SpriteBatchTest, EndWithoutBackendDoesNotThrow)
{
    SpriteBatch batch;
    EXPECT_NO_THROW(batch.End());
}

TEST(SpriteBatchTest, BeginWithoutBackendDoesNotThrow)
{
    SpriteBatch batch;
    EXPECT_NO_THROW(batch.Begin());
}

TEST(SpriteBatchTest, BeginSortBlendWithoutBackendDoesNotThrow)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    SpriteBatch batch;
    EXPECT_NO_THROW(batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend));
}

// --- Draw guard: throws when called before Begin (no-backend batch) ---

TEST(SpriteBatchTest, DrawXYBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(batch.Draw(tex, 0.0f, 0.0f), std::runtime_error);
}

TEST(SpriteBatchTest, DrawRectRectColorBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    Rectangle dest{0, 0, 32, 32};
    Rectangle src{0, 0, 32, 32};
    EXPECT_THROW(batch.Draw(tex, dest, src, Color::White), std::runtime_error);
}

TEST(SpriteBatchTest, DrawRectRectColorRotOriEffLayerBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    Rectangle dest{0, 0, 32, 32};
    Rectangle src{0, 0, 32, 32};
    EXPECT_THROW(
        batch.Draw(tex, dest, src, Color::White, 0.0f, Vector2::Zero,
                   SpriteEffects::None, 0.0f),
        std::runtime_error);
}

// --- DrawString guard: throws when called before Begin ---

static SpriteFont makeEmptyFont()
{
    return SpriteFont(
        Texture2D{},
        /*glyphBounds*/ {},
        /*cropping*/    {},
        /*characters*/  {},
        /*lineSpacing*/ 0,
        /*spacing*/     0.0f,
        /*kerningData*/ {},
        /*defaultChar*/ std::nullopt);
}

TEST(SpriteBatchTest, DrawStringStdStringBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    EXPECT_THROW(
        batch.DrawString(font, std::string("hi"), Vector2::Zero, Color::White),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawStringScalarScaleBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    EXPECT_THROW(
        batch.DrawString(font, std::string("hi"), Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawStringVec2ScaleBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    EXPECT_THROW(
        batch.DrawString(font, std::string("hi"), Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, Vector2(1.0f, 1.0f),
                         SpriteEffects::None, 0.0f),
        std::runtime_error);
}
