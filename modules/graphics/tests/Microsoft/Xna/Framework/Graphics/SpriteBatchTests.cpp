// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
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
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "RecordingSpriteBatchRenderer.hpp"

#include <cstddef>
#include <memory>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;
using System::ArgumentOutOfRangeException;

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
// SpriteBatch — no-renderer construction and guard throws
//
// A default-constructed SpriteBatch has no renderer and no graphics device.
// Begin/End/Draw overloads that guard on `begun` still fire before any
// renderer or texture access, so these tests do not need a GPU context.
// -----------------------------------------------------------------------

TEST(SpriteBatchTest, DefaultConstructorDoesNotThrow)
{
    EXPECT_NO_THROW(SpriteBatch batch);
}

TEST(SpriteBatchTest, EndWithoutBeginThrows)
{
    SpriteBatch batch;
    EXPECT_THROW(batch.End(), std::runtime_error);
}

TEST(SpriteBatchTest, BeginWithoutRendererDoesNotThrow)
{
    SpriteBatch batch;
    EXPECT_NO_THROW(batch.Begin());
}

TEST(SpriteBatchTest, BeginSortBlendWithoutRendererDoesNotThrow)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    SpriteBatch batch;
    EXPECT_NO_THROW(batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend));
}

TEST(SpriteBatchTest, BeginFiveParameterNullBlendUsesAlphaBlend)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    GraphicsDevice device;
    device.setBlendStateProperty(BlendState::Opaque);
    SpriteBatch batch(device);

    batch.Begin(SpriteSortMode::Deferred, static_cast<const BlendState*>(nullptr),
                nullptr, nullptr, nullptr);

    EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(),
              BlendState::AlphaBlend.getColorDestinationBlendProperty());
    batch.End();
}

TEST(SpriteBatchTest, BeginSixParameterNullBlendUsesAlphaBlend)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    GraphicsDevice device;
    device.setBlendStateProperty(BlendState::Opaque);
    SpriteBatch batch(device);

    batch.Begin(SpriteSortMode::Deferred, static_cast<const BlendState*>(nullptr),
                nullptr, nullptr, nullptr, nullptr);

    EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(),
              BlendState::AlphaBlend.getColorDestinationBlendProperty());
    batch.End();
}

TEST(SpriteBatchTest, BeginSevenParameterNullBlendUsesAlphaBlend)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Matrix;
    GraphicsDevice device;
    device.setBlendStateProperty(BlendState::Opaque);
    SpriteBatch batch(device);

    batch.Begin(SpriteSortMode::Deferred, static_cast<const BlendState*>(nullptr),
                nullptr, nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

    EXPECT_EQ(device.getBlendStateProperty().getColorDestinationBlendProperty(),
              BlendState::AlphaBlend.getColorDestinationBlendProperty());
    batch.End();
}

// --- Draw guard: throws when called before Begin (no-renderer batch) ---

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

// -----------------------------------------------------------------------
// Tasks 151–156: formerly-stubbed Draw overloads — guard throws
// -----------------------------------------------------------------------

TEST(SpriteBatchTest, DrawVec2ColorBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(batch.Draw(tex, Vector2::Zero, Color::White), std::runtime_error);
}

TEST(SpriteBatchTest, DrawVec2OptSrcColorBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(batch.Draw(tex, Vector2::Zero, std::optional<Rectangle>{}, Color::White),
                 std::runtime_error);
}

TEST(SpriteBatchTest, DrawVec2OptSrcColorRotOriScaleEffLayerBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(
        batch.Draw(tex, Vector2::Zero, std::optional<Rectangle>{}, Color::White,
                   0.0f, Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawVec2OptSrcColorRotOriVec2ScaleEffLayerBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(
        batch.Draw(tex, Vector2::Zero, std::optional<Rectangle>{}, Color::White,
                   0.0f, Vector2::Zero, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawRectColorBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(batch.Draw(tex, Rectangle(0, 0, 32, 32), Color::White), std::runtime_error);
}

TEST(SpriteBatchTest, DrawRectOptSrcColorBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(
        batch.Draw(tex, Rectangle(0, 0, 32, 32), std::optional<Rectangle>{}, Color::White),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawRectOptSrcColorRotOriEffLayerBeforeBeginThrows)
{
    SpriteBatch batch;
    Texture2D tex;
    EXPECT_THROW(
        batch.Draw(tex, Rectangle(0, 0, 32, 32), std::optional<Rectangle>{}, Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, 0.0f),
        std::runtime_error);
}

// -----------------------------------------------------------------------
// Tasks 157–159: DrawString(StringBuilder,…) — guard throws
// -----------------------------------------------------------------------

TEST(SpriteBatchTest, DrawStringStringBuilderBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    System::Text::StringBuilder sb;
    sb.Append("hi");
    EXPECT_THROW(batch.DrawString(font, sb, Vector2::Zero, Color::White),
                 std::runtime_error);
}

TEST(SpriteBatchTest, DrawStringStringBuilderScalarScaleBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    System::Text::StringBuilder sb;
    sb.Append("hi");
    EXPECT_THROW(
        batch.DrawString(font, sb, Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f),
        std::runtime_error);
}

TEST(SpriteBatchTest, DrawStringStringBuilderVec2ScaleBeforeBeginThrows)
{
    SpriteBatch batch;
    SpriteFont font = makeEmptyFont();
    System::Text::StringBuilder sb;
    sb.Append("hi");
    EXPECT_THROW(
        batch.DrawString(font, sb, Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f),
        std::runtime_error);
}

// -----------------------------------------------------------------------
// Tasks 161–166: sort-mode enum values and Begin/End guard behaviour
// -----------------------------------------------------------------------

TEST(SpriteBatchTest, BeginTwiceWithoutEndThrows)
{
    SpriteBatch batch;
    batch.Begin();
    EXPECT_THROW(batch.Begin(), std::runtime_error);
}

TEST(SpriteBatchTest, BeginEndBeginEndDoesNotThrow)
{
    SpriteBatch batch;
    EXPECT_NO_THROW({
        batch.Begin();
        batch.End();
        batch.Begin();
        batch.End();
    });
}

// -----------------------------------------------------------------------
// Task 411: mock/recording ISpriteBatchRenderer for deterministic unit tests
//
// Proves the new renderer-injecting constructor + RecordingSpriteBatchRenderer work end-to-end,
// without a GraphicsDevice or real graphics context. Tasks 412-416 build the actual
// per-SpriteSortMode assertions (Immediate flush timing, Deferred order preservation, Texture
// grouping, FrontToBack/BackToFront ordering) on top of this same infrastructure.
// -----------------------------------------------------------------------

TEST(SpriteBatchRendererInjectionTest, ConstructingWithRendererDoesNotThrow)
{
    EXPECT_NO_THROW(SpriteBatch batch(std::make_unique<RecordingSpriteBatchRenderer>()));
}

TEST(SpriteBatchRendererInjectionTest, BeginDispatchesToRenderer)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    batch.Begin();

    EXPECT_EQ(rec->beginCount, 1);
    EXPECT_EQ(rec->endCount, 0);
}

TEST(SpriteBatchRendererInjectionTest, EndDispatchesToRenderer)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    batch.Begin();
    batch.End();

    EXPECT_EQ(rec->beginCount, 1);
    EXPECT_EQ(rec->endCount, 1);
}

TEST(SpriteBatchRendererInjectionTest, DrawIsRecordedWithExactParameters)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(16, 16);
    Texture2D tex = Texture2D::CreateWithRendererForTests(16, 16,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    const Rectangle dest(10, 20, 16, 16);
    const Rectangle src(0, 0, 16, 16);
    const Color color(1, 2, 3, 4);
    const Vector2 origin(1.5f, 2.5f);

    batch.Begin();
    batch.Draw(tex, dest, src, color, 0.75f, origin, SpriteEffects::FlipHorizontally, 0.25f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 1u);
    const auto& call = rec->drawCalls[0];
    EXPECT_EQ(call.texture, &texRenderer);
    EXPECT_EQ(call.destinationRectangle, dest);
    EXPECT_EQ(call.sourceRectangle, src);
    EXPECT_EQ(call.color, color);
    EXPECT_FLOAT_EQ(call.rotation, 0.75f);
    EXPECT_EQ(call.origin, origin);
    EXPECT_EQ(call.effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(call.layerDepth, 0.25f);
}

// Task 922: the real, XNA-faithful 7th SpriteBatch::Draw overload -- Rectangle destination +
// optional (nullable) Rectangle source + rotation/origin/effects/depth. FNA's real equivalent
// takes Rectangle? sourceRectangle (src/Graphics/SpriteBatch.cs); the pre-existing CNAEXT overload
// wrongly required a non-optional Rectangle instead.

TEST(SpriteBatchRendererInjectionTest, DrawRectOptSrcRotOriEffLayerWithSourceIsRecordedWithExactParameters)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(16, 16);
    Texture2D tex = Texture2D::CreateWithRendererForTests(16, 16,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    const Rectangle dest(10, 20, 16, 16);
    const Rectangle src(2, 3, 8, 8);
    const Color color(1, 2, 3, 4);
    const Vector2 origin(1.5f, 2.5f);

    batch.Begin();
    batch.Draw(tex, dest, std::optional<Rectangle>(src), color, 0.75f, origin,
               SpriteEffects::FlipHorizontally, 0.25f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 1u);
    const auto& call = rec->drawCalls[0];
    EXPECT_EQ(call.texture, &texRenderer);
    EXPECT_EQ(call.destinationRectangle, dest);
    EXPECT_EQ(call.sourceRectangle, src);
    EXPECT_EQ(call.color, color);
    EXPECT_FLOAT_EQ(call.rotation, 0.75f);
    EXPECT_EQ(call.origin, origin);
    EXPECT_EQ(call.effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(call.layerDepth, 0.25f);
}

TEST(SpriteBatchRendererInjectionTest, DrawRectOptSrcRotOriEffLayerWithNulloptDrawsWholeTexture)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(16, 16);
    Texture2D tex = Texture2D::CreateWithRendererForTests(16, 16,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    const Rectangle dest(10, 20, 16, 16);
    const Color color(1, 2, 3, 4);
    const Vector2 origin(1.5f, 2.5f);

    batch.Begin();
    batch.Draw(tex, dest, std::optional<Rectangle>{}, color, 0.75f, origin,
               SpriteEffects::FlipHorizontally, 0.25f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 1u);
    const auto& call = rec->drawCalls[0];
    EXPECT_EQ(call.destinationRectangle, dest);
    EXPECT_EQ(call.sourceRectangle, Rectangle(0, 0, 16, 16));
    EXPECT_EQ(call.color, color);
    EXPECT_FLOAT_EQ(call.rotation, 0.75f);
    EXPECT_EQ(call.origin, origin);
    EXPECT_EQ(call.effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(call.layerDepth, 0.25f);
}

TEST(SpriteBatchRendererInjectionTest, DistinctTexturesProduceDistinctRecordedPointers)
{
    // SpriteSortMode::Texture (Task 414) sorts by the Texture2D*, and flushSingle() forwards
    // the underlying ITextureRenderer& — this test proves 2 distinct Texture2D instances are
    // observable as 2 distinct renderer pointers through the recording renderer, the precondition
    // Task 414's grouping assertion depends on.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer rendererA(4, 4);
    DummyTextureRenderer rendererB(8, 8);
    Texture2D texA = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithRendererForTests(8, 8,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererB, [](auto*) {}));

    batch.Begin();
    batch.Draw(texA, 0.0f, 0.0f);
    batch.Draw(texB, 0.0f, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[0].texture, &rendererA);
    EXPECT_EQ(rec->drawCalls[1].texture, &rendererB);
    EXPECT_NE(rec->drawCalls[0].texture, rec->drawCalls[1].texture);
}

// -----------------------------------------------------------------------
// Task 412: complete tests for SpriteSortMode::Immediate (Task 161 dependency)
//
// FNA/XNA's contract for SpriteSortMode::Immediate is that each sprite is submitted to the
// graphics device the instant Draw() is called, rather than being queued and flushed in a batch
// at End() (the behaviour every other sort mode uses). This must be observable BEFORE End() is
// ever called -- a renderer that only flushes at End() would pass every other SpriteBatch test in
// this file (which all read the recording after End()) while still violating Immediate's actual
// contract, so the assertion here is placed deliberately between Draw() and End().
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, ImmediateFlushesInsideDrawBeforeEnd)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(4, 4);
    Texture2D tex = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    batch.Draw(tex, 0.0f, 0.0f);

    // The critical assertion: the draw must already be recorded here, before End() runs.
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_EQ(rec->drawCalls[0].texture, &texRenderer);

    batch.End();

    // End() must not re-flush or duplicate the already-immediate draw.
    EXPECT_EQ(rec->drawCalls.size(), 1u);
}

TEST(SpriteBatchSortModeTest, ImmediateFlushesEachDrawSeparatelyInCallOrder)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer rendererA(4, 4);
    DummyTextureRenderer rendererB(8, 8);
    Texture2D texA = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithRendererForTests(8, 8,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererB, [](auto*) {}));

    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);

    batch.Draw(texA, 0.0f, 0.0f);
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_EQ(rec->drawCalls[0].texture, &rendererA);

    batch.Draw(texB, 0.0f, 0.0f);
    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[1].texture, &rendererB);

    batch.End();
    EXPECT_EQ(rec->drawCalls.size(), 2u);
}

TEST(SpriteBatchSortModeTest, DeferredDoesNotFlushBeforeEnd)
{
    // Negative control: proves the test methodology above actually discriminates Immediate from
    // the default (Deferred) mode, rather than every mode happening to flush eagerly.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(4, 4);
    Texture2D tex = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    batch.Begin(); // default: SpriteSortMode::Deferred
    batch.Draw(tex, 0.0f, 0.0f);

    EXPECT_EQ(rec->drawCalls.size(), 0u);

    batch.End();
    EXPECT_EQ(rec->drawCalls.size(), 1u);
}

TEST(SpriteBatchSortModeTest, DeferredRetainsTextureRendererThroughEnd)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    auto textureRenderer = std::make_shared<DummyTextureRenderer>(4, 4);
    std::weak_ptr<DummyTextureRenderer> lifetime = textureRenderer;

    batch.Begin();
    {
        Texture2D texture = Texture2D::CreateWithRendererForTests(
            4, 4, std::move(textureRenderer));
        batch.Draw(texture, 1.0f, 2.0f);
    }

    EXPECT_FALSE(lifetime.expired());
    EXPECT_NO_THROW(batch.End());
    EXPECT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_TRUE(lifetime.expired());
}

// -----------------------------------------------------------------------
// Task 413: complete tests for SpriteSortMode::Deferred (Task 162 dependency)
//
// FNA/XNA's contract for the default SpriteSortMode::Deferred is: no sort at all, sprites are
// delivered to the renderer in exactly their original Draw() submission order. flushBatch() only
// applies std::stable_sort for BackToFront/FrontToBack/Texture -- Deferred deliberately skips
// straight to iterating spriteQueue_ in insertion order. Task 412's DeferredDoesNotFlushBeforeEnd
// already covers the "not flushed before End()" half of Deferred's contract; this covers the
// "preserves submission order" half.
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, DeferredPreservesSubmissionOrder)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer rendererA(1, 1), rendererB(2, 2), rendererC(3, 3);
    Texture2D texA = Texture2D::CreateWithRendererForTests(1, 1,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithRendererForTests(2, 2,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererB, [](auto*) {}));
    Texture2D texC = Texture2D::CreateWithRendererForTests(3, 3,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererC, [](auto*) {}));

    // Deliberately submitted in an order that does NOT match any sort key (not sorted by
    // texture pointer, not sorted by any layerDepth since none is set here) -- if flushBatch()
    // accidentally applied a sort for Deferred, this submission order would very likely change.
    batch.Begin(); // default: SpriteSortMode::Deferred
    batch.Draw(texC, 0.0f, 0.0f);
    batch.Draw(texA, 0.0f, 0.0f);
    batch.Draw(texB, 0.0f, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);
    EXPECT_EQ(rec->drawCalls[0].texture, &rendererC);
    EXPECT_EQ(rec->drawCalls[1].texture, &rendererA);
    EXPECT_EQ(rec->drawCalls[2].texture, &rendererB);
}

// -----------------------------------------------------------------------
// Task 414: complete tests for SpriteSortMode::Texture (Task 163 dependency)
//
// FNA/XNA's contract for SpriteSortMode::Texture is: sprites are grouped by texture (to minimize
// GPU texture-bind state changes), sorted by raw texture reference. flushBatch() implements this
// as std::stable_sort(..., [](a,b){ return a.texture < b.texture; }) -- a *pointer* comparison,
// so which texture ends up "first" depends on runtime addresses, not something a test can predict
// in advance. The test below only asserts the 2 properties that are actually part of the
// contract and don't depend on address ordering: (1) all draws sharing a texture end up adjacent
// (grouped, no interleaving with the other texture), and (2) draws sharing the same texture keep
// their original relative submission order (stable_sort's stability, not a plain sort).
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, TextureGroupsDrawsByTextureAndPreservesGroupOrder)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer rendererA(1, 1), rendererB(2, 2);
    Texture2D texA = Texture2D::CreateWithRendererForTests(1, 1,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithRendererForTests(2, 2,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&rendererB, [](auto*) {}));

    // Alternating submission order (A, B, A) -- deliberately interleaved so a no-op/Deferred-like
    // implementation would leave B sandwiched between the 2 A draws. The 2 A draws use distinct
    // dest-rect X coordinates (1 and 3) purely as a submission-order marker, unrelated to sorting.
    batch.Begin(SpriteSortMode::Texture, Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend);
    batch.Draw(texA, Rectangle(1, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
    batch.Draw(texB, Rectangle(2, 0, 2, 2), Rectangle(0, 0, 2, 2), Color::White);
    batch.Draw(texA, Rectangle(3, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);

    std::vector<std::size_t> aIndices, bIndices;
    for (std::size_t i = 0; i < rec->drawCalls.size(); ++i)
    {
        if (rec->drawCalls[i].texture == &rendererA) aIndices.push_back(i);
        else if (rec->drawCalls[i].texture == &rendererB) bIndices.push_back(i);
    }
    ASSERT_EQ(aIndices.size(), 2u);
    ASSERT_EQ(bIndices.size(), 1u);

    // Grouped: the 2 A draws must be adjacent, regardless of whether A sorts before or after B
    // (that depends on runtime pointer addresses, not something this test can or should assume).
    EXPECT_EQ(aIndices[1], aIndices[0] + 1);

    // Stable within the group: the draw submitted first (dest X=1) must still precede the draw
    // submitted second (dest X=3) among the 2 A entries.
    EXPECT_EQ(rec->drawCalls[aIndices[0]].destinationRectangle.X, 1);
    EXPECT_EQ(rec->drawCalls[aIndices[1]].destinationRectangle.X, 3);
}

// -----------------------------------------------------------------------
// Task 415: complete tests for SpriteSortMode::FrontToBack (Task 164 dependency)
//
// FNA/XNA's contract for SpriteSortMode::FrontToBack is: sprites are sorted by ASCENDING
// layerDepth (smaller depth == closer to the camera == drawn first). flushBatch() implements
// this as std::stable_sort(..., [](a,b){ return a.layerDepth < b.layerDepth; }).
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, FrontToBackSortsByAscendingLayerDepth)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(4, 4);
    Texture2D tex = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    // plans/plan_graphics.md's own Task 164 example depths (0.5, 0.1, 0.9), deliberately submitted out
    // of order. Each dest-rect X coordinate (50/10/90) mirrors its own depth*100, purely as a
    // human-readable marker for which recorded call came from which Draw() call.
    batch.Begin(SpriteSortMode::FrontToBack, BlendState::AlphaBlend);
    batch.Draw(tex, Rectangle(50, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.5f);
    batch.Draw(tex, Rectangle(10, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
    batch.Draw(tex, Rectangle(90, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].layerDepth, 0.1f);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle.X, 10);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].layerDepth, 0.5f);
    EXPECT_EQ(rec->drawCalls[1].destinationRectangle.X, 50);
    EXPECT_FLOAT_EQ(rec->drawCalls[2].layerDepth, 0.9f);
    EXPECT_EQ(rec->drawCalls[2].destinationRectangle.X, 90);
}

// -----------------------------------------------------------------------
// Task 416: complete tests for SpriteSortMode::BackToFront (Task 165 dependency)
//
// FNA/XNA's contract for SpriteSortMode::BackToFront is: sprites are sorted by DESCENDING
// layerDepth (larger depth == farther from the camera == drawn first, so nearer sprites composite
// on top). flushBatch() implements this as
// std::stable_sort(..., [](a,b){ return a.layerDepth > b.layerDepth; }) -- the mirror image of
// Task 415's FrontToBack. Reuses Task 415's exact same 3 draws (same plans/plan_graphics.md Task 164/165
// example depths) with only the sort mode and expected order reversed, per plans/plan_graphics.md's own
// Task 165 note ("same 3 draws -- assert reverse delivery order").
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, BackToFrontSortsByDescendingLayerDepth)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRenderer(4, 4);
    Texture2D tex = Texture2D::CreateWithRendererForTests(4, 4,
        std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(&texRenderer, [](auto*) {}));

    batch.Begin(SpriteSortMode::BackToFront, BlendState::AlphaBlend);
    batch.Draw(tex, Rectangle(50, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.5f);
    batch.Draw(tex, Rectangle(10, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
    batch.Draw(tex, Rectangle(90, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White,
               0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].layerDepth, 0.9f);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle.X, 90);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].layerDepth, 0.5f);
    EXPECT_EQ(rec->drawCalls[1].destinationRectangle.X, 50);
    EXPECT_FLOAT_EQ(rec->drawCalls[2].layerDepth, 0.1f);
    EXPECT_EQ(rec->drawCalls[2].destinationRectangle.X, 10);
}

// -----------------------------------------------------------------------
// REMED-GFX-003: DrawString's axis-direction tables are indexed by (int)effects, and
// SpriteEffects is a composable [Flags] enum -- FlipHorizontally|FlipVertically (value 3) is a
// real, reachable value. The pre-fix tables were sized for 3 entries only, an out-of-bounds
// stack read for the combined value. Verified structurally: combined flip must mirror X the same
// way FlipHorizontally alone does, and Y the same way FlipVertically alone does (FNA's own
// axisDirX/axisIsMirroredX[3] == [1]'s values; axisDirY/axisIsMirroredY[3] == [2]'s values) --
// rotation=0 keeps the two axes independent, so this cross-check needs no hand-computed numbers.
// -----------------------------------------------------------------------

namespace
{
    SpriteFont makeSingleGlyphFontWithRenderer(
        const std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>& texRenderer)
    {
        Texture2D atlas = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);
        std::vector<Rectangle> glyphs   = { Rectangle(0, 0, 8, 12) };
        std::vector<Rectangle> cropping = { Rectangle(0, 0, 8, 12) };
        std::vector<SharpRuntime::charcs> chars = { u'A' };
        std::vector<Microsoft::Xna::Framework::Vector3> kern =
            { Microsoft::Xna::Framework::Vector3(1.0f, 8.0f, 2.0f) };
        return SpriteFont(atlas, glyphs, cropping, chars, /*lineSpacing=*/16, /*spacing=*/0.0f,
                          kern, std::nullopt);
    }

    std::vector<RecordingSpriteBatchRenderer::DrawCall> drawSingleCharWithEffects(
        const SpriteFont& font, SpriteEffects effects)
    {
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        RecordingSpriteBatchRenderer* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));
        batch.Begin();
        batch.DrawString(font, std::string("A"), Vector2(10.0f, 20.0f), Color::White,
                         0.0f, Vector2::Zero, 1.0f, effects, 0.0f);
        batch.End();
        return rec->drawCalls;
    }

    SpriteFont makeUnitGlyphFontWithRenderer(
        const std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>& texRenderer)
    {
        Texture2D atlas = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);
        std::vector<Rectangle> glyphs   = { Rectangle(0, 0, 1, 1) };
        std::vector<Rectangle> cropping = { Rectangle(0, 0, 1, 1) };
        std::vector<SharpRuntime::charcs> chars = { u'A' };
        std::vector<Microsoft::Xna::Framework::Vector3> kern =
            { Microsoft::Xna::Framework::Vector3(0.0f, 1.0f, 0.0f) };
        return SpriteFont(atlas, glyphs, cropping, chars, /*lineSpacing=*/1, /*spacing=*/0.0f,
                          kern, std::nullopt);
    }

    template<typename TAction>
    void expectNumericArgumentOutOfRange(const char* parameterName, TAction&& action)
    {
        try
        {
            action();
            FAIL() << "Expected System::ArgumentOutOfRangeException for " << parameterName;
        }
        catch (const ArgumentOutOfRangeException& exception)
        {
            EXPECT_EQ(exception.getParamNameProperty(), parameterName);
        }
        catch (...)
        {
            FAIL() << "Expected System::ArgumentOutOfRangeException for " << parameterName;
        }
    }
}

// -----------------------------------------------------------------------
// REMED-GFX-057: public floating-point inputs that feed SpriteBatch's integer
// destination bridge must be rejected before any undefined float-to-int cast.
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
// XNA 4.0 and FNA carry a sprite's destination through the batch as floats
// (FNA's SpriteBatch.PushSprite takes float destinationX/Y/W/H), so a sprite
// drawn at a fractional position lands between pixels and the active sampler
// filters its edges. CNA used to quantise the destination inside SpriteBatch,
// which flattened every fractional sprite onto whole pixels.
// -----------------------------------------------------------------------

TEST(SpriteBatchSubPixelDestinationTest, VectorPositionDrawKeepsItsFractionalPosition)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

    batch.Begin();
    batch.Draw(texture, Vector2(12.25f, -9.75f), Color::White);
    batch.Draw(texture, 3.5f, 7.125f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationX, 12.25f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationY, -9.75f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationWidth, 16.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationHeight, 16.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].destinationX, 3.5f);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].destinationY, 7.125f);
}

TEST(SpriteBatchSubPixelDestinationTest, FractionalScaleKeepsItsFractionalDestinationSize)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

    batch.Begin();
    batch.Draw(texture, Vector2(1.5f, 2.5f), std::nullopt, Color::White,
               0.0f, Vector2::Zero, 0.75f, SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2(1.5f, 2.5f), std::nullopt, Color::White,
               0.0f, Vector2::Zero, Vector2(0.25f, 1.75f), SpriteEffects::None, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationWidth, 12.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationHeight, 12.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].destinationWidth, 4.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[1].destinationHeight, 28.0f);
}

TEST(SpriteBatchSubPixelDestinationTest, RectangleDestinationOverloadsStayExactlyOnPixels)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

    batch.Begin();
    batch.Draw(texture, Rectangle(5, 6, 7, 8), Color::White);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationX, 5.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationY, 6.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationWidth, 7.0f);
    EXPECT_FLOAT_EQ(rec->drawCalls[0].destinationHeight, 8.0f);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle, Rectangle(5, 6, 7, 8));
}

/** A renderer that has not adopted the sub-pixel overload must still be driven correctly. */
namespace
{
    class IntegerOnlySpriteBatchRenderer : public CNA::Internal::Renderers::ISpriteBatchRenderer
    {
    public:
        std::vector<Rectangle> destinations;

        void Begin() override {}
        void End() override {}
        void Draw(const CNA::Internal::Renderers::ITextureRenderer&, float, float) override {}
        void Draw(const CNA::Internal::Renderers::ITextureRenderer&,
                  const Rectangle& destinationRectangle,
                  const Rectangle&,
                  const Color&) override
        {
            destinations.push_back(destinationRectangle);
        }
        void Draw(const CNA::Internal::Renderers::ITextureRenderer&,
                  const Rectangle& destinationRectangle,
                  const Rectangle&,
                  const Color&,
                  float,
                  const Vector2&,
                  SpriteEffects,
                  float) override
        {
            destinations.push_back(destinationRectangle);
        }
    };
}

TEST(SpriteBatchSubPixelDestinationTest, ARendererWithoutTheSubPixelOverloadStillReceivesPixels)
{
    auto renderer = std::make_unique<IntegerOnlySpriteBatchRenderer>();
    IntegerOnlySpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

    batch.Begin();
    batch.Draw(texture, Vector2(12.75f, -9.75f), Color::White);
    batch.End();

    ASSERT_EQ(rec->destinations.size(), 1u);
    EXPECT_EQ(rec->destinations[0], Rectangle(12, -9, 16, 16));
}

TEST(SpriteBatchNumericInputTest, DrawXYDefinesFiniteInt32BoundariesAndTruncation)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

    const float positiveLimit =
        std::nextafter(2147483648.0f, 0.0f);
    const float negativeLimit = -2147483648.0f;
    const float belowNegativeLimit =
        std::nextafter(negativeLimit, -std::numeric_limits<float>::infinity());

    batch.Begin();
    // CABI-38: non-finite coordinates are XNA-valid and travel through; only finite values too
    // large to be a representable destination are refused.
    batch.Draw(texture, std::numeric_limits<float>::quiet_NaN(), 0.0f);
    batch.Draw(texture, std::numeric_limits<float>::infinity(), 0.0f);
    batch.Draw(texture, 0.0f, -std::numeric_limits<float>::infinity());
    expectNumericArgumentOutOfRange("x", [&] {
        batch.Draw(texture, std::numeric_limits<float>::max(), 0.0f);
    });
    expectNumericArgumentOutOfRange("x", [&] {
        batch.Draw(texture, 2147483648.0f, 0.0f);
    });
    expectNumericArgumentOutOfRange("y", [&] {
        batch.Draw(texture, 0.0f, belowNegativeLimit);
    });

    batch.Draw(texture, positiveLimit, negativeLimit);
    batch.Draw(texture, std::numeric_limits<float>::denorm_min(),
               -std::numeric_limits<float>::denorm_min());
    batch.Draw(texture, 12.75f, -9.75f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 6u);
    // The three non-finite draws arrive first, and arrive unaltered.
    EXPECT_TRUE(std::isnan(rec->drawCalls[0].destinationX));
    EXPECT_TRUE(std::isinf(rec->drawCalls[1].destinationX));
    EXPECT_TRUE(std::isinf(rec->drawCalls[2].destinationY));
    EXPECT_EQ(rec->drawCalls[3].destinationRectangle,
              Rectangle(2147483520, -2147483647 - 1, 16, 16));
    EXPECT_EQ(rec->drawCalls[4].destinationRectangle, Rectangle(0, 0, 16, 16));
    EXPECT_EQ(rec->drawCalls[5].destinationRectangle, Rectangle(12, -9, 16, 16));
}

TEST(SpriteBatchNumericInputTest, EveryVectorPositionDrawFamilyRejectsInvalidCoordinates)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);
    const std::optional<Rectangle> source(Rectangle(2, 3, 8, 6));

    batch.Begin();
    // CABI-38: the three non-finite positions are carried through; only the finite value too large
    // to be a representable destination is refused.
    batch.Draw(texture,
               Vector2(std::numeric_limits<float>::quiet_NaN(), 0.0f),
               Color::White);
    batch.Draw(texture,
               Vector2(0.0f, std::numeric_limits<float>::infinity()),
               source, Color::White);
    batch.Draw(texture,
               Vector2(-std::numeric_limits<float>::infinity(), 0.0f),
               source, Color::White, 0.0f, Vector2::Zero, 1.0f,
               SpriteEffects::None, 0.0f);
    expectNumericArgumentOutOfRange("position", [&] {
        batch.Draw(texture,
                   Vector2(0.0f, std::numeric_limits<float>::max()),
                   source, Color::White, 0.0f, Vector2::Zero, Vector2::One,
                   SpriteEffects::None, 0.0f);
    });
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);
    EXPECT_TRUE(std::isnan(rec->drawCalls[0].destinationX));
    EXPECT_TRUE(std::isinf(rec->drawCalls[1].destinationY));
    EXPECT_TRUE(std::isinf(rec->drawCalls[2].destinationX));
    rec->drawCalls.clear();

    // A rejected Draw leaves the Begin/End state balanced and reusable.
    batch.Begin();
    batch.Draw(texture, Vector2(1.75f, -2.75f), Color::White);
    batch.Draw(texture, Vector2(3.75f, -4.75f), source, Color::White);
    batch.Draw(texture, Vector2(5.75f, -6.75f), source, Color::White,
               0.0f, Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2(7.75f, -8.75f), source, Color::White,
               0.0f, Vector2::Zero, Vector2::One, SpriteEffects::None, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 4u);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle, Rectangle(1, -2, 16, 16));
    EXPECT_EQ(rec->drawCalls[1].destinationRectangle, Rectangle(3, -4, 8, 6));
    EXPECT_EQ(rec->drawCalls[2].destinationRectangle, Rectangle(5, -6, 8, 6));
    EXPECT_EQ(rec->drawCalls[3].destinationRectangle, Rectangle(7, -8, 8, 6));
}

TEST(SpriteBatchNumericInputTest, ScalarAndVectorScaleDistinguishInvalidFromRepresentableValues)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);
    const std::optional<Rectangle> source(Rectangle(2, 3, 16, 16));

    const float positiveDimensionLimit =
        std::nextafter(2147483648.0f, 0.0f);
    const float scalarPositiveLimit = positiveDimensionLimit / 16.0f;
    const float scalarNegativeLimit = -2147483648.0f / 16.0f;
    const float belowScalarNegativeLimit =
        std::nextafter(scalarNegativeLimit, -std::numeric_limits<float>::infinity());

    batch.Begin();
    // CABI-38: a non-finite scale is XNA-valid and reaches the vertex path; the refusals below are
    // the finite-but-unrepresentable ones, which is a separate contract and unchanged.
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, std::numeric_limits<float>::quiet_NaN(),
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, std::numeric_limits<float>::infinity(),
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, -std::numeric_limits<float>::infinity(),
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, Vector2(1.0f, std::numeric_limits<float>::quiet_NaN()),
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero,
               Vector2(std::numeric_limits<float>::infinity(), 1.0f),
               SpriteEffects::None, 0.0f);
    // float::max() as a scale is finite, but the 16-pixel source multiplies it to infinity, and an
    // infinite destination is now carried through rather than refused. The refusal below is the
    // real remaining case: a value whose product stays finite and is still unrepresentable.
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, std::numeric_limits<float>::max(),
               SpriteEffects::None, 0.0f);
    expectNumericArgumentOutOfRange("scale", [&] {
        batch.Draw(texture, Vector2::Zero, source, Color::White,
                   0.0f, Vector2::Zero, Vector2(1.0f, belowScalarNegativeLimit),
                   SpriteEffects::None, 0.0f);
    });

    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, std::numeric_limits<float>::denorm_min(),
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, scalarPositiveLimit,
               SpriteEffects::None, 0.0f);
    batch.Draw(texture, Vector2::Zero, source, Color::White,
               0.0f, Vector2::Zero, scalarNegativeLimit,
               SpriteEffects::None, 0.0f);

    const Color tint(1, 2, 3, 4);
    const Vector2 origin(1.5f, 2.5f);
    batch.Draw(texture, Vector2(10.75f, -20.75f), source, tint,
               0.25f, origin, Vector2(-1.5f, 0.5f),
               SpriteEffects::FlipHorizontally, 0.75f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 10u);
    // The five non-finite scales arrive first, unaltered, and so does the one that overflowed.
    EXPECT_TRUE(std::isnan(rec->drawCalls[0].destinationWidth));
    EXPECT_TRUE(std::isinf(rec->drawCalls[1].destinationWidth));
    EXPECT_TRUE(std::isinf(rec->drawCalls[2].destinationWidth));
    EXPECT_TRUE(std::isnan(rec->drawCalls[3].destinationHeight));
    EXPECT_TRUE(std::isinf(rec->drawCalls[4].destinationWidth));
    EXPECT_TRUE(std::isinf(rec->drawCalls[5].destinationWidth));

    EXPECT_EQ(rec->drawCalls[6].destinationRectangle, Rectangle(0, 0, 0, 0));
    EXPECT_EQ(rec->drawCalls[7].destinationRectangle,
              Rectangle(0, 0, 2147483520, 2147483520));
    EXPECT_EQ(rec->drawCalls[8].destinationRectangle,
              Rectangle(0, 0, -2147483647 - 1, -2147483647 - 1));

    const auto& ordinary = rec->drawCalls[9];
    EXPECT_EQ(ordinary.destinationRectangle, Rectangle(10, -20, -24, 8));
    EXPECT_EQ(ordinary.sourceRectangle, source.value());
    EXPECT_EQ(ordinary.color, tint);
    EXPECT_FLOAT_EQ(ordinary.rotation, 0.25f);
    EXPECT_EQ(ordinary.origin, origin);
    EXPECT_EQ(ordinary.effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(ordinary.layerDepth, 0.75f);
}

// CABI-38: non-finite values are XNA-valid and reach the vertex path.
//
// This case asserted the opposite until CABI-38 -- that NaN and the infinities were refused with
// ArgumentOutOfRangeException. fixcnacs.md Phase 5 asked for XNA's behaviour, and the decompiled
// XNA SpriteBatch validates nothing at all: no IsNaN, no IsInfinity, no finiteness check anywhere.
// The refusal was a CNA invention.
//
// Accepting them is not enough on its own, so this checks they arrive: the recorder keeps the
// unrounded float destination SpriteBatch actually delivered, and a value that were silently
// clamped or dropped on the way would show up here as a finite number.
TEST(SpriteBatchNumericInputTest, EveryDrawStringFamilyCarriesNonFiniteValuesThrough)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeUnitGlyphFontWithRenderer(texRenderer);
    System::Text::StringBuilder builder;
    builder.Append("A");

    batch.Begin();
    batch.DrawString(font, std::string("A"), Vector2(nan, 0.0f), Color::White);
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, inf, SpriteEffects::None, 0.0f);
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, Vector2(1.0f, -inf), SpriteEffects::None, 0.0f);
    batch.DrawString(font, builder, Vector2(0.0f, inf), Color::White);
    batch.DrawString(font, builder, Vector2::Zero, Color::White,
                     nan, Vector2::Zero, 1.0f, SpriteEffects::None, 0.0f);
    batch.DrawString(font, builder, Vector2::Zero, Color::White,
                     0.0f, Vector2(inf, 0.0f), Vector2::One, SpriteEffects::None, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 6u);
    EXPECT_TRUE(std::isnan(rec->drawCalls[0].destinationX)) << "a NaN position must survive";
    EXPECT_TRUE(std::isinf(rec->drawCalls[1].destinationWidth)) << "an infinite scale must survive";
    EXPECT_TRUE(std::isinf(rec->drawCalls[2].destinationHeight))
        << "a negative infinite scale component must survive";
    EXPECT_TRUE(std::isinf(rec->drawCalls[3].destinationY)) << "an infinite position must survive";
    EXPECT_TRUE(std::isnan(rec->drawCalls[4].rotation)) << "a NaN rotation must survive";
    // An infinite origin rotates into the destination rather than staying in the origin field.
    EXPECT_FALSE(std::isfinite(rec->drawCalls[5].destinationX))
        << "an infinite origin must reach the destination";
}

// The Int32 destination range is a separate contract from finiteness, and it survives CABI-38: a
// finite value too large to be a representable destination is still refused, with the parameter
// named. Only the non-finite refusals went away.
TEST(SpriteBatchNumericInputTest, DrawStringStillRefusesUnrepresentableFiniteDestinations)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeUnitGlyphFontWithRenderer(texRenderer);
    System::Text::StringBuilder builder;
    builder.Append("A");

    batch.Begin();
    expectNumericArgumentOutOfRange("position", [&] {
        batch.DrawString(font, std::string("A"),
                         Vector2(std::numeric_limits<float>::max(), 0.0f),
                         Color::White);
    });
    expectNumericArgumentOutOfRange("scale", [&] {
        batch.DrawString(font, builder, Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, std::numeric_limits<float>::max(),
                         SpriteEffects::None, 0.0f);
    });
    batch.End();

    EXPECT_TRUE(rec->drawCalls.empty());

    // A rejected DrawString leaves the Begin/End state balanced and reusable.
    batch.Begin();
    batch.DrawString(font, std::string("A"), Vector2(10.25f, -4.25f), Color::White);
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, std::numeric_limits<float>::denorm_min(),
                     SpriteEffects::None, 0.0f);
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, Vector2(-1.5f, 0.5f),
                     SpriteEffects::FlipHorizontally, 0.25f);
    batch.DrawString(font, builder, Vector2(20.25f, 30.25f), Color::White);
    batch.DrawString(font, builder, Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, 1.25f, SpriteEffects::None, 0.5f);
    batch.DrawString(font, builder, Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, Vector2(1.25f, -1.5f),
                     SpriteEffects::FlipVertically, 0.75f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 6u);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle, Rectangle(10, -4, 1, 1));
    EXPECT_EQ(rec->drawCalls[1].destinationRectangle, Rectangle(0, 0, 0, 0));
    EXPECT_EQ(rec->drawCalls[2].destinationRectangle, Rectangle(0, 0, -2, 1));
    EXPECT_EQ(rec->drawCalls[2].effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(rec->drawCalls[2].layerDepth, 0.25f);
    EXPECT_EQ(rec->drawCalls[3].destinationRectangle, Rectangle(20, 30, 1, 1));
    EXPECT_EQ(rec->drawCalls[4].destinationRectangle, Rectangle(0, 0, 1, 1));
    EXPECT_FLOAT_EQ(rec->drawCalls[4].layerDepth, 0.5f);
    EXPECT_EQ(rec->drawCalls[5].destinationRectangle, Rectangle(0, 0, 1, -2));
    EXPECT_EQ(rec->drawCalls[5].effects, SpriteEffects::FlipVertically);
    EXPECT_FLOAT_EQ(rec->drawCalls[5].layerDepth, 0.75f);
}

// CABI-38: a NaN layer depth must sort, not corrupt.
//
// This is the reason CNA could not simply accept non-finite values before. Both depth sorts used a
// bare `<` on layerDepth, and NaN compares false against everything: `a < b` and `b < a` are both
// false, which violates the strict weak ordering std::stable_sort requires. That is undefined
// behaviour -- libstdc++ can walk off the end of the range -- not merely a surprising order. XNA
// has no such hazard because it sorts through IComparable<float>, which defines a total order with
// NaN below everything.
//
// CompareOrdered gives CNA the same total order, so this exercises what used to be UB: enough
// sprites to take stable_sort's real (non-insertion) path, with NaN depths scattered among finite
// ones, in both sort modes.
TEST(SpriteBatchNumericInputTest, NonFiniteLayerDepthsSortWithoutCorruptingTheQueue)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (const SpriteSortMode mode : {SpriteSortMode::BackToFront, SpriteSortMode::FrontToBack})
    {
        auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
        RecordingSpriteBatchRenderer* rec = renderer.get();
        SpriteBatch batch(std::move(renderer));

        DummyTextureRenderer texRendererRaw(16, 16);
        auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
            &texRendererRaw, [](auto*) {});
        Texture2D texture = Texture2D::CreateWithRendererForTests(16, 16, texRenderer);

        // Well past stable_sort's insertion-sort threshold, so the merge path really runs.
        constexpr int kCount = 64;
        batch.Begin(mode, BlendState::AlphaBlend);
        for (int index = 0; index < kCount; ++index)
        {
            const float depth = (index % 4 == 0) ? nan
                              : (index % 4 == 1) ? -inf
                              : (index % 4 == 2) ? inf
                                                 : static_cast<float>(index) / kCount;
            batch.Draw(texture, Vector2(static_cast<float>(index), 0.0f), std::nullopt,
                       Color::White, 0.0f, Vector2::Zero, 1.0f, SpriteEffects::None, depth);
        }
        batch.End();

        // Nothing lost, nothing duplicated: the queue survived the sort.
        ASSERT_EQ(rec->drawCalls.size(), static_cast<std::size_t>(kCount));

        // Every NaN depth is at one end -- the total order puts NaN below everything, so they lead
        // in FrontToBack and trail in BackToFront.
        const std::size_t nanCount = static_cast<std::size_t>((kCount + 3) / 4);
        for (std::size_t index = 0; index < nanCount; ++index)
        {
            const std::size_t at = (mode == SpriteSortMode::FrontToBack)
                                 ? index
                                 : rec->drawCalls.size() - 1 - index;
            EXPECT_TRUE(std::isnan(rec->drawCalls[at].layerDepth))
                << "NaN depths must gather at one end, index " << at;
        }
    }
}

TEST(SpriteBatchNumericInputTest, DrawStringAcceptsExactInt32RoundedBoundaries)
{
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    RecordingSpriteBatchRenderer* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));

    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeUnitGlyphFontWithRenderer(texRenderer);

    const float positiveLimit = std::nextafter(2147483648.0f, 0.0f);
    const float negativeLimit = -2147483648.0f;
    const float belowNegativeLimit =
        std::nextafter(negativeLimit, -std::numeric_limits<float>::infinity());

    batch.Begin();
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, positiveLimit,
                     SpriteEffects::None, 0.0f);
    batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                     0.0f, Vector2::Zero, negativeLimit,
                     SpriteEffects::None, 0.0f);
    expectNumericArgumentOutOfRange("scale", [&] {
        batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, 2147483648.0f,
                         SpriteEffects::None, 0.0f);
    });
    expectNumericArgumentOutOfRange("scale", [&] {
        batch.DrawString(font, std::string("A"), Vector2::Zero, Color::White,
                         0.0f, Vector2::Zero, belowNegativeLimit,
                         SpriteEffects::None, 0.0f);
    });
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[0].destinationRectangle,
              Rectangle(0, 0, 2147483520, 2147483520));
    EXPECT_EQ(rec->drawCalls[1].destinationRectangle,
              Rectangle(0, 0, -2147483647 - 1, -2147483647 - 1));
}

TEST(SpriteBatchDrawStringSpriteEffectsTest, CombinedFlipMirrorsXLikeHorizontalAlone)
{
    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeSingleGlyphFontWithRenderer(texRenderer);

    auto horiz = drawSingleCharWithEffects(font, SpriteEffects::FlipHorizontally);
    auto both  = drawSingleCharWithEffects(
        font, SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically);

    ASSERT_EQ(horiz.size(), 1u);
    ASSERT_EQ(both.size(), 1u);
    EXPECT_EQ(both[0].destinationRectangle.X, horiz[0].destinationRectangle.X);
}

TEST(SpriteBatchDrawStringSpriteEffectsTest, CombinedFlipMirrorsYLikeVerticalAlone)
{
    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeSingleGlyphFontWithRenderer(texRenderer);

    auto vert = drawSingleCharWithEffects(font, SpriteEffects::FlipVertically);
    auto both = drawSingleCharWithEffects(
        font, SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically);

    ASSERT_EQ(vert.size(), 1u);
    ASSERT_EQ(both.size(), 1u);
    EXPECT_EQ(both[0].destinationRectangle.Y, vert[0].destinationRectangle.Y);
}

TEST(SpriteBatchDrawStringSpriteEffectsTest, CombinedFlipDiffersFromNone)
{
    // Sanity check that the combined value actually changes placement relative to None --
    // guards against a fix that merely avoids the OOB read without applying real flip math.
    DummyTextureRenderer texRendererRaw(16, 16);
    auto texRenderer = std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer>(
        &texRendererRaw, [](auto*) {});
    SpriteFont font = makeSingleGlyphFontWithRenderer(texRenderer);

    auto none = drawSingleCharWithEffects(font, SpriteEffects::None);
    auto both = drawSingleCharWithEffects(
        font, SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically);

    ASSERT_EQ(none.size(), 1u);
    ASSERT_EQ(both.size(), 1u);
    EXPECT_NE(both[0].destinationRectangle.X, none[0].destinationRectangle.X);
    EXPECT_NE(both[0].destinationRectangle.Y, none[0].destinationRectangle.Y);
}

// -----------------------------------------------------------------------
// REMED-GFX-003: SpriteEffects flag operators (matches GestureType's established convention)
// -----------------------------------------------------------------------

TEST(SpriteEffectsOperatorsTest, OrCombinesFlags)
{
    const SpriteEffects combined = SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically;
    EXPECT_EQ(static_cast<int>(combined), 3);
}

TEST(SpriteEffectsOperatorsTest, AndMasksFlags)
{
    const SpriteEffects combined = SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically;
    EXPECT_EQ(combined & SpriteEffects::FlipHorizontally, SpriteEffects::FlipHorizontally);
    EXPECT_EQ(combined & SpriteEffects::FlipVertically, SpriteEffects::FlipVertically);
    EXPECT_EQ(SpriteEffects::FlipHorizontally & SpriteEffects::FlipVertically, SpriteEffects::None);
}

TEST(SpriteEffectsOperatorsTest, OrAssignCombinesInPlace)
{
    SpriteEffects effects = SpriteEffects::FlipHorizontally;
    effects |= SpriteEffects::FlipVertically;
    EXPECT_EQ(static_cast<int>(effects), 3);
}

TEST(SpriteEffectsOperatorsTest, AndAssignMasksInPlace)
{
    SpriteEffects effects = SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically;
    effects &= SpriteEffects::FlipHorizontally;
    EXPECT_EQ(effects, SpriteEffects::FlipHorizontally);
}


using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// -----------------------------------------------------------------------
// A texture belongs to the GraphicsDevice that created it. Drawing one through another device's
// SpriteBatch has to be refused at Draw(), before the sprite is queued -- not at the renderer seam.
// A renderer-side refusal is reached from flushBatch(), i.e. from inside End(), which leaves
// `begun` true AND the offending sprite in the queue: the next End() refuses it again and the
// SpriteBatch is unusable for good. On NANOVG the same mistake is worse than a lost draw, because
// its per-NVGcontext image handles collide across contexts and a foreign texture names a valid but
// different image, so an unchecked draw silently paints the wrong picture.
// -----------------------------------------------------------------------

TEST(SpriteBatchCrossDeviceTest, ImmediateRefusesATextureFromAnotherDeviceAndStaysUsable)
{
    GraphicsDevice owning;
    GraphicsDevice other;
    Texture2D foreign(owning, 4, 4, false, SurfaceFormat::Color);
    Texture2D own(other, 4, 4, false, SurfaceFormat::Color);

    SpriteBatch batch(other);
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    EXPECT_THROW(batch.Draw(foreign, Vector2(0.0f, 0.0f), Color::White),
                 System::InvalidOperationException);
    // The batch is still open and still works: the refusal happened before anything was queued.
    EXPECT_NO_THROW(batch.Draw(own, Vector2(0.0f, 0.0f), Color::White));
    EXPECT_NO_THROW(batch.End());
    // And it can be begun again, which a wedged batch could not.
    EXPECT_NO_THROW(batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend));
    EXPECT_NO_THROW(batch.End());
}

TEST(SpriteBatchCrossDeviceTest, DeferredRefusesAtDrawRatherThanWedgingTheBatchAtEnd)
{
    GraphicsDevice owning;
    GraphicsDevice other;
    Texture2D foreign(owning, 4, 4, false, SurfaceFormat::Color);
    Texture2D own(other, 4, 4, false, SurfaceFormat::Color);

    SpriteBatch batch(other);
    batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
    EXPECT_THROW(batch.Draw(foreign, Vector2(0.0f, 0.0f), Color::White),
                 System::InvalidOperationException);
    EXPECT_NO_THROW(batch.Draw(own, Vector2(0.0f, 0.0f), Color::White));
    // The decisive one: End() must not rediscover the refused sprite, because it was never queued.
    EXPECT_NO_THROW(batch.End());
    EXPECT_NO_THROW(batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend));
    EXPECT_NO_THROW(batch.End());
}

TEST(SpriteBatchCrossDeviceTest, ATextureFromTheSameDeviceIsUnaffected)
{
    GraphicsDevice device;
    Texture2D texture(device, 4, 4, false, SurfaceFormat::Color);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
    EXPECT_NO_THROW(batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White));
    EXPECT_NO_THROW(batch.End());
}
