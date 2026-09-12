// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-029: exercise the production low-level rlgl SpriteBatch path only
// through the public XNA API, using exact backbuffer pixels as the observable contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;
    constexpr float kHalfPi = 1.57079632679489661923f;
}

class RlglSpriteBatchTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglSpriteBatchTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        SpriteBatch batch(device);

        Effect unsupportedEffect(device);
        bool customEffectRejected = false;
        try
        {
            batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                        &SamplerState::PointClamp, nullptr, nullptr,
                        &unsupportedEffect);
        }
        catch (const System::NotSupportedException&)
        {
            customEffectRejected = true;
        }
        ExpectTrue("source SpriteBatch Effect is rejected at Begin until RLGL-050",
                   customEffectRejected);

        Texture2D pattern(device, 2, 2);
        const std::array<Color, 4> patternPixels{
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 255, 255)};
        pattern.SetData(patternPixels.data(), static_cast<int>(patternPixels.size()));

        Texture2D red(device, 1, 1);
        Texture2D green(device, 1, 1);
        Texture2D blue(device, 1, 1);
        const Color redPixel(255, 0, 0, 255);
        const Color greenPixel(0, 255, 0, 255);
        const Color bluePixel(0, 0, 255, 255);
        red.SetData(&redPixel, 1);
        green.SetData(&greenPixel, 1);
        blue.SetData(&bluePixel, 1);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(pattern, Rectangle(4, 4, 32, 24), Rectangle(0, 0, 2, 2), Color::White);
        batch.End();
        ExpectPixel("scaled texture keeps top-left row order",
                    Rectangle(8, 8, 1, 1), Color::Red);
        ExpectPixel("scaled texture reaches bottom-right texel",
                    Rectangle(30, 22, 1, 1), Color::White);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(pattern, Rectangle(4, 4, 20, 12), Rectangle(1, 0, 1, 1), Color::White);
        batch.Draw(pattern, Rectangle(28, 4, 20, 12), Rectangle(0, 0, 2, 2),
                   Color::White, 0.0f, Vector2::Zero,
                   SpriteEffects::FlipHorizontally, 0.0f);
        batch.Draw(pattern, Rectangle(4, 20, 20, 12), Rectangle(0, 0, 2, 2),
                   Color::White, 0.0f, Vector2::Zero,
                   SpriteEffects::FlipVertically, 0.0f);
        batch.End();
        ExpectPixel("source rectangle selects one exact texel",
                    Rectangle(12, 9, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("SpriteEffects horizontal flip swaps sampled columns",
                    Rectangle(31, 7, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("SpriteEffects vertical flip swaps sampled rows",
                    Rectangle(7, 23, 1, 1), Color::Blue);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::BackToFront, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(red, Rectangle(4, 4, 16, 16), Rectangle(0, 0, 1, 1),
                   Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        batch.Draw(blue, Rectangle(4, 4, 16, 16), Rectangle(0, 0, 1, 1),
                   Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        batch.End();
        ExpectPixel("BackToFront ordering leaves the nearest sprite visible",
                    Rectangle(10, 10, 1, 1), Color::Red);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::FrontToBack, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(red, Rectangle(4, 4, 16, 16), Rectangle(0, 0, 1, 1),
                   Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        batch.Draw(blue, Rectangle(4, 4, 16, 16), Rectangle(0, 0, 1, 1),
                   Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        batch.End();
        ExpectPixel("FrontToBack ordering reverses the observable overlap",
                    Rectangle(10, 10, 1, 1), Color::Blue);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Texture, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(blue, Rectangle(36, 4, 8, 8), Color::White);
        batch.Draw(red, Rectangle(4, 4, 8, 8), Color::White);
        batch.Draw(green, Rectangle(20, 4, 8, 8), Color::White);
        batch.End();
        ExpectPixel("Texture sort preserves each sprite's geometry while regrouping",
                    Rectangle(7, 7, 1, 1), Color::Red);
        ExpectPixel("Texture sort submits every regrouped texture",
                    Rectangle(39, 7, 1, 1), Color::Blue);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        for (int sprite = 0; sprite < 16384; ++sprite)
            batch.Draw(red, Rectangle(0, 0, 1, 1), Color::White);
        batch.End();
        ExpectPixel("a texture group larger than the 16-bit batch flushes without loss",
                    Rectangle(0, 0, 1, 1), Color::Red);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(red, Rectangle(4, 4, 16, 16), Color::White);
        BlendState greenOnly = BlendState::Opaque;
        greenOnly.setColorWriteChannelsProperty(ColorWriteChannels::Green);
        device.setBlendStateProperty(greenOnly);
        batch.Draw(green, Rectangle(4, 4, 16, 16), Color::White);
        batch.End();
        ExpectPixel("Immediate mode observes pipeline changes between Draw calls",
                    Rectangle(10, 10, 1, 1), Color(255, 255, 0, 255));

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr, nullptr,
                    Matrix::CreateTranslation(12.0f, 7.0f, 0.0f));
        batch.Draw(red, Rectangle(0, 0, 8, 8), Color::White);
        batch.End();
        ExpectPixel("Begin transform matrix translates sprite geometry",
                    Rectangle(14, 9, 1, 1), Color::Red);
        ExpectPixel("transform leaves the untransformed location untouched",
                    Rectangle(2, 2, 1, 1), Color::Black);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(red, Rectangle(40, 10, 20, 10), Rectangle(0, 0, 1, 1),
                   Color::White, kHalfPi, Vector2::Zero, SpriteEffects::None, 0.0f);
        batch.End();
        ExpectPixel("rotation uses the requested source-space origin",
                    Rectangle(35, 20, 1, 1), Color::Red);
        ExpectPixel("rotation does not leave an axis-aligned fallback quad",
                    Rectangle(50, 15, 1, 1), Color::Black);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointWrap, nullptr, nullptr);
        batch.Draw(pattern, Rectangle(4, 4, 40, 12), Rectangle(0, 0, 4, 1), Color::White);
        batch.End();
        ExpectPixel("SpriteBatch forwards Wrap instead of clamping source UVs",
                    Rectangle(29, 8, 1, 1), Color::Red);

        device.setViewportProperty(Viewport(20, 10, 20, 20));
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(blue, Rectangle(0, 0, 20, 20), Color::White);
        batch.End();
        ExpectPixel("custom Viewport makes SpriteBatch coordinates viewport-local",
                    Rectangle(25, 15, 1, 1), Color::Blue);
        ExpectPixel("custom Viewport clips outside its physical rectangle",
                    Rectangle(5, 5, 1, 1), Color::Black);
        device.setViewportProperty(Viewport(0, 0, kWidth, kHeight));

        device.Clear(Color::Black);
        batch.Begin();
        batch.Draw(red, Rectangle(4, 4, 16, 16), Color(128, 0, 0, 128));
        batch.End();
        ExpectPixel("default AlphaBlend consumes premultiplied sprite tint",
                    Rectangle(10, 10, 1, 1), Color(128, 0, 0, 255), 1);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.Draw(red, Vector2(4.75f, 4.75f), Color::White);
        batch.End();
        ExpectPixel("fractional destination remains unrounded through the GPU upload",
                    Rectangle(5, 5, 1, 1), Color::Red);
        ExpectPixel("fractional destination does not fall back to an integer rectangle",
                    Rectangle(4, 4, 1, 1), Color::Black);

        device.Clear(Color::Black);
        device.setScissorRectangleProperty(Rectangle(12, 8, 16, 12));
        RasterizerState clipped = RasterizerState::CullCounterClockwise;
        clipped.setScissorTestEnableProperty(true);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, &clipped);
        batch.Draw(blue, Rectangle(0, 0, kWidth, kHeight), Color::White);
        batch.End();
        ExpectPixel("SpriteBatch respects the caller's enabled scissor rectangle",
                    Rectangle(16, 12, 1, 1), Color::Blue);
        ExpectPixel("SpriteBatch clipping rejects pixels outside the scissor rectangle",
                    Rectangle(6, 6, 1, 1), Color::Black);

        device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);

        Texture2D fontAtlas(device, 16, 8);
        std::vector<Color> fontPixels(16 * 8, Color::White);
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 8; x < 16; ++x)
                fontPixels[static_cast<std::size_t>(y * 16 + x)] = greenPixel;
        }
        fontAtlas.SetData(fontPixels.data(), static_cast<int>(fontPixels.size()));
        SpriteFont font(
            fontAtlas,
            {Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8)},
            {Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8)},
            {u'A', u'B'}, 10, 0.0f,
            {Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f)},
            std::nullopt);

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.DrawString(font, "A\nB", Vector2(4.0f, 4.0f), Color::White);
        batch.End();
        ExpectPixel("SpriteFont renders its first glyph through the RLGL batch",
                    Rectangle(7, 7, 1, 1), Color::White);
        ExpectPixel("SpriteFont newline and glyph atlas selection remain intact",
                    Rectangle(7, 17, 1, 1), Color(0, 255, 0, 255));

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque,
                    &SamplerState::PointClamp, nullptr, nullptr);
        batch.DrawString(font, "AB", Vector2(4.0f, 4.0f), Color::White, 0.0f,
                         Vector2::Zero, Vector2(1.0f, 1.0f),
                         SpriteEffects::FlipHorizontally, 0.0f);
        batch.End();
        ExpectPixel("SpriteFont horizontal flip mirrors glyph order",
                    Rectangle(7, 7, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("SpriteFont horizontal flip retains the other glyph",
                    Rectangle(15, 7, 1, 1), Color::White);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglSpriteBatchTest game;
    game.Run();
    return game.getResultProperty();
}
