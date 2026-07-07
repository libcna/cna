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

#include "RecordingSpriteBatchBackend.hpp"

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
using CNA::Internal::Backends::DummyTextureBackend;
using CNA::Internal::Backends::RecordingSpriteBatchBackend;

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

TEST(SpriteBatchTest, EndWithoutBeginThrows)
{
    SpriteBatch batch;
    EXPECT_THROW(batch.End(), std::runtime_error);
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
// Task 411: mock/recording ISpriteBatchBackend for deterministic unit tests
//
// Proves the new backend-injecting constructor + RecordingSpriteBatchBackend work end-to-end,
// without a GraphicsDevice or real graphics context. Tasks 412-416 build the actual
// per-SpriteSortMode assertions (Immediate flush timing, Deferred order preservation, Texture
// grouping, FrontToBack/BackToFront ordering) on top of this same infrastructure.
// -----------------------------------------------------------------------

TEST(SpriteBatchBackendInjectionTest, ConstructingWithBackendDoesNotThrow)
{
    EXPECT_NO_THROW(SpriteBatch batch(std::make_unique<RecordingSpriteBatchBackend>()));
}

TEST(SpriteBatchBackendInjectionTest, BeginDispatchesToBackend)
{
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    batch.Begin();

    EXPECT_EQ(rec->beginCount, 1);
    EXPECT_EQ(rec->endCount, 0);
}

TEST(SpriteBatchBackendInjectionTest, EndDispatchesToBackend)
{
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    batch.Begin();
    batch.End();

    EXPECT_EQ(rec->beginCount, 1);
    EXPECT_EQ(rec->endCount, 1);
}

TEST(SpriteBatchBackendInjectionTest, DrawIsRecordedWithExactParameters)
{
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend texBackend(16, 16);
    Texture2D tex = Texture2D::CreateWithBackendForTests(16, 16,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&texBackend, [](auto*) {}));

    const Rectangle dest(10, 20, 16, 16);
    const Rectangle src(0, 0, 16, 16);
    const Color color(1, 2, 3, 4);
    const Vector2 origin(1.5f, 2.5f);

    batch.Begin();
    batch.Draw(tex, dest, src, color, 0.75f, origin, SpriteEffects::FlipHorizontally, 0.25f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 1u);
    const auto& call = rec->drawCalls[0];
    EXPECT_EQ(call.texture, &texBackend);
    EXPECT_EQ(call.destinationRectangle, dest);
    EXPECT_EQ(call.sourceRectangle, src);
    EXPECT_EQ(call.color, color);
    EXPECT_FLOAT_EQ(call.rotation, 0.75f);
    EXPECT_EQ(call.origin, origin);
    EXPECT_EQ(call.effects, SpriteEffects::FlipHorizontally);
    EXPECT_FLOAT_EQ(call.layerDepth, 0.25f);
}

TEST(SpriteBatchBackendInjectionTest, DistinctTexturesProduceDistinctRecordedPointers)
{
    // SpriteSortMode::Texture (Task 414) sorts by the Texture2D*, and flushSingle() forwards
    // the underlying ITextureBackend& — this test proves 2 distinct Texture2D instances are
    // observable as 2 distinct backend pointers through the recording backend, the precondition
    // Task 414's grouping assertion depends on.
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend backendA(4, 4);
    DummyTextureBackend backendB(8, 8);
    Texture2D texA = Texture2D::CreateWithBackendForTests(4, 4,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithBackendForTests(8, 8,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendB, [](auto*) {}));

    batch.Begin();
    batch.Draw(texA, 0.0f, 0.0f);
    batch.Draw(texB, 0.0f, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[0].texture, &backendA);
    EXPECT_EQ(rec->drawCalls[1].texture, &backendB);
    EXPECT_NE(rec->drawCalls[0].texture, rec->drawCalls[1].texture);
}

// -----------------------------------------------------------------------
// Task 412: complete tests for SpriteSortMode::Immediate (Task 161 dependency)
//
// FNA/XNA's contract for SpriteSortMode::Immediate is that each sprite is submitted to the
// graphics device the instant Draw() is called, rather than being queued and flushed in a batch
// at End() (the behaviour every other sort mode uses). This must be observable BEFORE End() is
// ever called -- a backend that only flushes at End() would pass every other SpriteBatch test in
// this file (which all read the recording after End()) while still violating Immediate's actual
// contract, so the assertion here is placed deliberately between Draw() and End().
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, ImmediateFlushesInsideDrawBeforeEnd)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend texBackend(4, 4);
    Texture2D tex = Texture2D::CreateWithBackendForTests(4, 4,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&texBackend, [](auto*) {}));

    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    batch.Draw(tex, 0.0f, 0.0f);

    // The critical assertion: the draw must already be recorded here, before End() runs.
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_EQ(rec->drawCalls[0].texture, &texBackend);

    batch.End();

    // End() must not re-flush or duplicate the already-immediate draw.
    EXPECT_EQ(rec->drawCalls.size(), 1u);
}

TEST(SpriteBatchSortModeTest, ImmediateFlushesEachDrawSeparatelyInCallOrder)
{
    using Microsoft::Xna::Framework::Graphics::BlendState;

    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend backendA(4, 4);
    DummyTextureBackend backendB(8, 8);
    Texture2D texA = Texture2D::CreateWithBackendForTests(4, 4,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithBackendForTests(8, 8,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendB, [](auto*) {}));

    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);

    batch.Draw(texA, 0.0f, 0.0f);
    ASSERT_EQ(rec->drawCalls.size(), 1u);
    EXPECT_EQ(rec->drawCalls[0].texture, &backendA);

    batch.Draw(texB, 0.0f, 0.0f);
    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[1].texture, &backendB);

    batch.End();
    EXPECT_EQ(rec->drawCalls.size(), 2u);
}

TEST(SpriteBatchSortModeTest, DeferredDoesNotFlushBeforeEnd)
{
    // Negative control: proves the test methodology above actually discriminates Immediate from
    // the default (Deferred) mode, rather than every mode happening to flush eagerly.
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend texBackend(4, 4);
    Texture2D tex = Texture2D::CreateWithBackendForTests(4, 4,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&texBackend, [](auto*) {}));

    batch.Begin(); // default: SpriteSortMode::Deferred
    batch.Draw(tex, 0.0f, 0.0f);

    EXPECT_EQ(rec->drawCalls.size(), 0u);

    batch.End();
    EXPECT_EQ(rec->drawCalls.size(), 1u);
}

// -----------------------------------------------------------------------
// Task 413: complete tests for SpriteSortMode::Deferred (Task 162 dependency)
//
// FNA/XNA's contract for the default SpriteSortMode::Deferred is: no sort at all, sprites are
// delivered to the backend in exactly their original Draw() submission order. flushBatch() only
// applies std::stable_sort for BackToFront/FrontToBack/Texture -- Deferred deliberately skips
// straight to iterating spriteQueue_ in insertion order. Task 412's DeferredDoesNotFlushBeforeEnd
// already covers the "not flushed before End()" half of Deferred's contract; this covers the
// "preserves submission order" half.
// -----------------------------------------------------------------------

TEST(SpriteBatchSortModeTest, DeferredPreservesSubmissionOrder)
{
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend backendA(1, 1), backendB(2, 2), backendC(3, 3);
    Texture2D texA = Texture2D::CreateWithBackendForTests(1, 1,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithBackendForTests(2, 2,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendB, [](auto*) {}));
    Texture2D texC = Texture2D::CreateWithBackendForTests(3, 3,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendC, [](auto*) {}));

    // Deliberately submitted in an order that does NOT match any sort key (not sorted by
    // texture pointer, not sorted by any layerDepth since none is set here) -- if flushBatch()
    // accidentally applied a sort for Deferred, this submission order would very likely change.
    batch.Begin(); // default: SpriteSortMode::Deferred
    batch.Draw(texC, 0.0f, 0.0f);
    batch.Draw(texA, 0.0f, 0.0f);
    batch.Draw(texB, 0.0f, 0.0f);
    batch.End();

    ASSERT_EQ(rec->drawCalls.size(), 3u);
    EXPECT_EQ(rec->drawCalls[0].texture, &backendC);
    EXPECT_EQ(rec->drawCalls[1].texture, &backendA);
    EXPECT_EQ(rec->drawCalls[2].texture, &backendB);
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
    auto backend = std::make_unique<RecordingSpriteBatchBackend>();
    RecordingSpriteBatchBackend* rec = backend.get();
    SpriteBatch batch(std::move(backend));

    DummyTextureBackend backendA(1, 1), backendB(2, 2);
    Texture2D texA = Texture2D::CreateWithBackendForTests(1, 1,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendA, [](auto*) {}));
    Texture2D texB = Texture2D::CreateWithBackendForTests(2, 2,
        std::shared_ptr<CNA::Internal::Backends::ITextureBackend>(&backendB, [](auto*) {}));

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
        if (rec->drawCalls[i].texture == &backendA) aIndices.push_back(i);
        else if (rec->drawCalls[i].texture == &backendB) bIndices.push_back(i);
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
