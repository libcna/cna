// SPDX-License-Identifier: MS-PL
// ISSUE-003: prepared public-state witnesses, not yet compiled or runtime-validated.
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "System/InvalidOperationException.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

class SpriteTextureBindingTest : public testing::Test
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters{}};
    Texture2D first{device, 1, 1};
    Texture2D second{device, 1, 1};
    SpriteBatch batch{device};

    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu, Fna3d, WebGPU, DirectX9);
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        const Color white = Color::White;
        first.SetData(&white, 1);
        second.SetData(&white, 1);
    }
    void Draw(Texture2D& texture, float depth = 0.0f)
    {
        batch.Draw(texture, Rectangle(0, 0, 1, 1), std::nullopt, Color::White,
                   0.0f, Vector2::Zero, SpriteEffects::None, depth);
    }
};

TEST_F(SpriteTextureBindingTest, DeferredPublishesLastTextureOnlyWhenFlushed)
{
    batch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    Draw(first);
    Draw(second);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &second);
}

TEST_F(SpriteTextureBindingTest, ImmediatePublishesEachDrawAndEndRetainsIt)
{
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    EXPECT_EQ(device.getTexturesProperty()[0], nullptr);
    Draw(first);
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
    Draw(second);
    EXPECT_EQ(device.getTexturesProperty()[0], &second);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &second);
}

TEST_F(SpriteTextureBindingTest, EmptyBatchPreservesAnExistingBinding)
{
    device.getTexturesProperty()(0, &first);
    for (const SpriteSortMode mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
    {
        batch.Begin(mode, BlendState::AlphaBlend);
        EXPECT_EQ(device.getTexturesProperty()[0], &first);
        batch.End();
        EXPECT_EQ(device.getTexturesProperty()[0], &first);
    }
}

TEST_F(SpriteTextureBindingTest, FrontToBackRetainsLastRenderedNotLastSubmittedTexture)
{
    batch.Begin(SpriteSortMode::FrontToBack, BlendState::AlphaBlend);
    Draw(first, 0.9f);
    Draw(second, 0.1f);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
}

TEST_F(SpriteTextureBindingTest, BackToFrontRetainsLastRenderedNotLastSubmittedTexture)
{
    batch.Begin(SpriteSortMode::BackToFront, BlendState::AlphaBlend);
    Draw(first, 0.1f);
    Draw(second, 0.9f);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
}

TEST_F(SpriteTextureBindingTest, ExplicitUnbindingRestoresSetDataAfterSpriteFlush)
{
    batch.Begin();
    Draw(first);
    batch.End();
    EXPECT_EQ(device.getTexturesProperty()[0], &first);
    const Color blue = Color::Blue;
    EXPECT_THROW(first.SetData(&blue, 1), System::InvalidOperationException);
    device.getTexturesProperty()(0, nullptr);
    EXPECT_NO_THROW(first.SetData(&blue, 1));
    Color readBack = Color::Transparent;
    first.GetData(&readBack, 1);
    EXPECT_EQ(readBack, blue);
}
} // namespace
