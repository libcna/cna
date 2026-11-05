// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include "RecordingSpriteBatchRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DummyTextureRenderer;
using CNA::Internal::Renderers::RecordingSpriteBatchRenderer;

template<class Action>
void ExpectOrderingMessage(Action action, const char* expected)
{
    try { action(); FAIL() << "Expected InvalidOperationException"; }
    catch (const System::InvalidOperationException& error)
    { EXPECT_EQ(error.getMessageProperty(), expected); }
}

TEST(SpriteBatchMessageTest, PairErrorsUseRefreshResourceStrings)
{
    // Refresh Begin IL_0008-0012, End IL_0008-0012, InternalDraw IL_001b-0025;
    // literal strings extracted by key from Microsoft.Xna.Framework.dll resources.
    auto renderer = std::make_unique<RecordingSpriteBatchRenderer>();
    auto* rec = renderer.get();
    SpriteBatch batch(std::move(renderer));
    const auto texture = Texture2D::CreateWithRendererForTests(
        8, 8, std::make_shared<DummyTextureRenderer>(8, 8));
    SpriteFont font(texture, {Rectangle(0, 0, 4, 6)}, {Rectangle(0, 0, 4, 6)},
                    {u'A'}, 8, 0.0f, {Vector3(0, 4, 0)}, std::nullopt);
    ExpectOrderingMessage([&] { batch.End(); },
        "Begin must be called successfully before End can be called.");
    ExpectOrderingMessage([&] { batch.Draw(texture, Vector2::Zero, Color::White); },
        "Begin must be called successfully before a Draw can be called.");
    ExpectOrderingMessage([&] { batch.DrawString(font, std::string("A"),
                                               Vector2::Zero, Color::White); },
        "Begin must be called successfully before a Draw can be called.");
    batch.Begin();
    ExpectOrderingMessage([&] { batch.Begin(); },
        "Begin cannot be called again until End has been successfully called.");
    batch.End();
    EXPECT_EQ(rec->beginCount, 1);
    EXPECT_EQ(rec->endCount, 1);
    EXPECT_TRUE(rec->drawCalls.empty());
}

TEST(SpriteBatchMessageDeviceTest, CrossBatchImmediateRefusalsUseSameRefreshMessage)
{
    // Refresh Begin IL_005a-0064 and IL_008f-0099 use the SAME resource key.
    GraphicsDevice device;
    SpriteBatch first(device), second(device);
    constexpr auto message = "Nesting more than one SpriteBatch.Begin when using a "
                             "SpriteBatch with SpriteSortMode.Immediate is not allowed.";
    first.Begin();
    ExpectOrderingMessage([&] { second.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend); },
                          message);
    first.End();
    second.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    ExpectOrderingMessage([&] { first.Begin(); }, message);
    second.End();
    first.Begin();
    first.End();
    EXPECT_THROW(first.End(), System::InvalidOperationException);
    EXPECT_THROW(second.End(), System::InvalidOperationException);
}
}
