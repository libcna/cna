// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

TEST(SpriteBatchApiValueTest, SevenDrawOverloadsPreservePermissiveArgumentsAndDefaults)
{
    // Refresh Draw wrappers -> InternalDraw IL_006b-0182 (source/scale),
    // IL_0183-01c8 (origin, rotation, depth, flags, color). No rectangle/scale/depth bounds checks.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    const auto texture = Texture2D::CreateWithRendererForTests(
        8, 4, std::make_shared<DummyTextureRenderer>(8, 4));
    const Vector2 position(7.5f, 9.25f), origin(1, 2);
    const Rectangle destination(7, 9, 10, 20), source(-2, -1, 12, 6);
    const Color tint(13, 29, 47, 89);
    const auto both = SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically;
    batch.Begin();
    batch.Draw(texture, position, tint);
    batch.Draw(texture, position, std::optional<Rectangle>(source), tint);
    batch.Draw(texture, position, std::optional<Rectangle>(source), tint,
               0.25f, origin, 0.0f, both, -0.5f);
    batch.Draw(texture, position, std::optional<Rectangle>(source), tint,
               0.25f, origin, Vector2(-2, 3), both, 1.5f);
    batch.Draw(texture, destination, tint);
    batch.Draw(texture, destination, std::optional<Rectangle>(source), tint);
    batch.Draw(texture, destination, std::optional<Rectangle>(source), tint,
               0.25f, origin, both, 1.5f);
    EXPECT_TRUE(rec->drawCalls.empty());
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(), 7u);
    // Exact binary32 arithmetic: 12*0=0, 6*0=0, 12*(-2)=-24, 6*3=18.
    const float widths[] = {8, 12, 0, -24, 10, 10, 10};
    const float heights[] = {4, 6, 0, 18, 20, 20, 20};
    const float depths[] = {0, 0, -0.5f, 1.5f, 0, 0, 1.5f};
    for (int i = 0; i < 7; ++i)
    {
        SCOPED_TRACE(i);
        const auto& call = rec->drawCalls[i];
        const bool transformed = i == 2 || i == 3 || i == 6;
        EXPECT_EQ(call.texture, texture.GetRendererWeak().lock().get());
        EXPECT_EQ(call.color, tint);
        EXPECT_EQ(call.sourceRectangle, i == 0 || i == 4 ? Rectangle(0, 0, 8, 4) : source);
        EXPECT_EQ(call.destinationX, i < 4 ? 7.5f : 7.0f);
        EXPECT_EQ(call.destinationY, i < 4 ? 9.25f : 9.0f);
        EXPECT_EQ(call.destinationWidth, widths[i]);
        EXPECT_EQ(call.destinationHeight, heights[i]);
        EXPECT_EQ(call.origin, transformed ? origin : Vector2::Zero);
        EXPECT_EQ(call.rotation, transformed ? 0.25f : 0.0f);
        EXPECT_EQ(call.effects, transformed ? both : SpriteEffects::None);
        EXPECT_EQ(call.layerDepth, depths[i]);
    }
}

TEST(SpriteBatchApiValueTest, SourceDimensionsAreNotClippedOrNormalised)
{
    // Refresh InternalDraw IL_006b-0144 copies source and multiplies dimensions;
    // zero/negative source sizes are not bounds errors. No pixels are inferred here.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    const auto texture = Texture2D::CreateWithRendererForTests(
        8, 4, std::make_shared<DummyTextureRenderer>(8, 4));
    const Rectangle sources[] = {Rectangle(20, 30, 0, 6), Rectangle(20, 30, -4, -2)};
    batch.Begin();
    for (const auto& source : sources)
        batch.Draw(texture, Vector2::Zero, std::optional<Rectangle>(source), Color::White,
                   0, Vector2::Zero, Vector2(-2, 3), SpriteEffects::None, 0);
    batch.End();
    ASSERT_EQ(rec->drawCalls.size(), 2u);
    EXPECT_EQ(rec->drawCalls[0].sourceRectangle, sources[0]);
    EXPECT_EQ(rec->drawCalls[1].sourceRectangle, sources[1]);
    EXPECT_EQ(rec->drawCalls[0].destinationWidth, 0);  // 0 * -2
    EXPECT_EQ(rec->drawCalls[0].destinationHeight, 18); // 6 * 3
    EXPECT_EQ(rec->drawCalls[1].destinationWidth, 8);  // -4 * -2
    EXPECT_EQ(rec->drawCalls[1].destinationHeight, -6); // -2 * 3
}

class TransformRecordingRenderer final : public RecordingSpriteBatchRenderer
{
public:
    Matrix matrix{};
    Effect* effect = nullptr;
    void SetTransformMatrix(const Matrix& value) override { matrix = value; }
    void SetCustomEffect(Effect* value) override { effect = value; }
};

TEST(SpriteBatchApiValueTest, BeginCopiesTransformAndShortOverloadsResetIt)
{
    // Refresh Begin(7) IL_0040-0043 copies Matrix by value. Begin wrappers IL_0007
    // or IL_0009/000a supply Identity. Tests the front-end renderer contract, not GPU projection.
    auto renderer = std::make_unique<TransformRecordingRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    Matrix transform = Matrix::getIdentityProperty();
    transform.M11 = 2; transform.M22 = 3; transform.M33 = 4;
    transform.M41 = 5; transform.M42 = 6; transform.M43 = 7;
    batch.Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, nullptr, nullptr, transform);
    transform.M41 = 99;
    EXPECT_EQ(rec->matrix.M11, 2);
    EXPECT_EQ(rec->matrix.M22, 3);
    EXPECT_EQ(rec->matrix.M33, 4);
    EXPECT_EQ(rec->matrix.M41, 5);
    EXPECT_EQ(rec->matrix.M42, 6);
    EXPECT_EQ(rec->matrix.M43, 7);
    batch.End();
    for (int overload = 0; overload < 6; ++overload)
    {
        SCOPED_TRACE(overload);
        // Seed a nonidentity matrix before each short overload so one reset cannot hide another.
        batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                    nullptr, nullptr, transform);
        batch.End();
        switch (overload)
        {
        case 0: batch.Begin(); break;
        case 1: batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend); break;
        case 2: batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                            nullptr, nullptr, nullptr); break;
        case 3: batch.Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, nullptr); break;
        case 4: batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                            nullptr, nullptr, nullptr, nullptr); break;
        case 5: batch.Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, nullptr, nullptr); break;
        }
        EXPECT_EQ(rec->matrix, Matrix::getIdentityProperty());
        EXPECT_EQ(rec->effect, nullptr);
        batch.End();
    }
}

class LifetimeRenderer final : public RecordingSpriteBatchRenderer
{
public:
    explicit LifetimeRenderer(bool& destroyed) : destroyed_(destroyed) {}
    ~LifetimeRenderer() override { destroyed_ = true; }
private:
    bool& destroyed_;
};

TEST(SpriteBatchApiValueTest, DisposePublishesStateBeforeCallbackAndPreservesMetadata)
{
    // Refresh SpriteBatch.Dispose IL_000b-001f releases children before finally/base;
    // GraphicsResource.~GraphicsResource IL_0008-0021 sets IsDisposed BEFORE Disposing.
    // Name/Tag use local fields (get IL_001e-0024, set IL_0020-0027) without disposed guards.
    bool destroyed = false;
    int events = 0;
    SpriteBatch batch(std::make_unique<LifetimeRenderer>(destroyed));
    SpriteBatch tag; // Concrete System::Object used only as a non-owning Tag sentinel.
    EXPECT_FALSE(batch.getIsDisposedProperty());
    EXPECT_TRUE(batch.getNameProperty().empty()); // std::string maps the default null name to empty.
    EXPECT_EQ(batch.getTagProperty(), nullptr);
    batch.setNameProperty("ui batch");
    batch.setTagProperty(&tag);
    batch.Disposing += [&](System::Object* sender, const System::EventArgs&) {
        ++events;
        EXPECT_EQ(sender, &batch);
        EXPECT_TRUE(batch.getIsDisposedProperty());
        EXPECT_TRUE(destroyed);
        EXPECT_EQ(batch.getNameProperty(), "ui batch");
        EXPECT_EQ(batch.getTagProperty(), &tag);
        batch.Dispose(); // Reentry must not raise twice.
        throw std::runtime_error("subscriber failure");
    };
    EXPECT_THROW(batch.Dispose(), std::runtime_error);
    EXPECT_EQ(events, 1);
    EXPECT_TRUE(batch.getIsDisposedProperty());
    EXPECT_NO_THROW(batch.Dispose());
    EXPECT_EQ(events, 1);
    EXPECT_EQ(batch.getNameProperty(), "ui batch");
    EXPECT_EQ(batch.getTagProperty(), &tag);
    batch.setNameProperty("retired batch");
    batch.setTagProperty(nullptr);
    EXPECT_EQ(batch.getNameProperty(), "retired batch");
    EXPECT_EQ(batch.getTagProperty(), nullptr);
}
}
