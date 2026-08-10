// SPDX-License-Identifier: MS-PL
// SKIA-39: Complete the public SpriteBatch overload audit under the Skia renderer.
//
// The shared sdlrenderer_spritebatch_overloads_test.cpp already executes the first nine public
// Texture2D Draw overloads through this renderer. This focused Skia test covers the remaining
// optional-source-rectangle overload and every DrawString overload: std::string and StringBuilder,
// in their basic, uniform-scale, and non-uniform-scale forms. A hand-built atlas keeps the test on
// the common SpriteFont/SpriteBatch path rather than invoking Skia text rendering.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    bool colourMatch(Color got, Color want, int tolerance = 40)
    {
        const auto within = [tolerance](int a, int b) {
            return a >= b - tolerance && a <= b + tolerance;
        };
        return within(got.getRProperty(), want.getRProperty())
            && within(got.getGProperty(), want.getGProperty())
            && within(got.getBProperty(), want.getBProperty());
    }
}

class SkiaSpriteBatchRemainingOverloadsTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> atlas_;
    std::unique_ptr<Texture2D> splitTexture_;
    std::unique_ptr<SpriteFont> font_;
    bool done_ = false;
    int result_ = 0;

    void checkPixel(int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle onePixel(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&onePixel, &actual, 0, 1);
        const bool ok = colourMatch(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        if (!ok)
            result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // A single opaque 8x8 glyph gives exact, layout-dependent bounds for each DrawString call.
        atlas_ = std::make_unique<Texture2D>(device, 8, 8);
        std::vector<Color> whitePixels(64, kWhite);
        atlas_->SetData(whitePixels.data(), static_cast<int>(whitePixels.size()));
        const std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8) };
        const std::vector<Rectangle> cropping = { Rectangle(0, 0, 8, 8) };
        const std::vector<SharpRuntime::charcs> characters = { u'A' };
        const std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f) };
        font_ = std::make_unique<SpriteFont>(*atlas_, glyphBounds, cropping, characters,
                                             8, 0.0f, kerning,
                                             std::optional<SharpRuntime::charcs>(std::nullopt));

        // An asymmetric texture makes the last Texture2D Draw overload's flip observable.
        std::vector<Color> splitPixels(16 * 8, kBlack);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                splitPixels[static_cast<std::size_t>(y * 16 + x)] = x < 8 ? kRed : kBlue;
        splitTexture_ = std::make_unique<Texture2D>(device, 16, 8);
        splitTexture_->SetData(splitPixels.data(), static_cast<int>(splitPixels.size()));
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBlack);
        device.setBlendStateProperty(BlendState::Opaque);

        System::Text::StringBuilder builder("A");
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);

        // std::string DrawString: basic, scalar scale, then Vector2 scale.
        sb_->DrawString(*font_, "A", Vector2(2.0f, 2.0f), kWhite);
        sb_->DrawString(*font_, "A", Vector2(20.0f, 2.0f), kWhite, 0.0f, Vector2::Zero,
                        2.0f, SpriteEffects::None, 0.0f);
        sb_->DrawString(*font_, "A", Vector2(45.0f, 2.0f), kWhite, 0.0f, Vector2::Zero,
                        Vector2(3.0f, 2.0f), SpriteEffects::None, 0.0f);

        // Corresponding StringBuilder overloads must use the identical shared glyph path.
        sb_->DrawString(*font_, builder, Vector2(2.0f, 28.0f), kWhite);
        sb_->DrawString(*font_, builder, Vector2(20.0f, 28.0f), kWhite, 0.0f, Vector2::Zero,
                        2.0f, SpriteEffects::None, 0.0f);
        sb_->DrawString(*font_, builder, Vector2(45.0f, 28.0f), kWhite, 0.0f, Vector2::Zero,
                        Vector2(3.0f, 2.0f), SpriteEffects::None, 0.0f);

        // The tenth and final public Texture2D Draw overload: optional source rectangle, transform,
        // effects, and layer depth. FlipHorizontally makes the left output half blue, not red.
        sb_->Draw(*splitTexture_, Rectangle(2, 55, 32, 16),
                  std::optional<Rectangle>(Rectangle(0, 0, 16, 8)), kWhite,
                  0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sb_->End();

        checkPixel(6, 6, kWhite, "std::string basic DrawString");
        checkPixel(28, 10, kWhite, "std::string scalar-scale DrawString");
        checkPixel(60, 10, kWhite, "std::string Vector2-scale DrawString");
        checkPixel(6, 32, kWhite, "StringBuilder basic DrawString");
        checkPixel(28, 36, kWhite, "StringBuilder scalar-scale DrawString");
        checkPixel(60, 36, kWhite, "StringBuilder Vector2-scale DrawString");
        checkPixel(10, 62, kBlue, "final Texture2D Draw overload flips left half to blue");
        checkPixel(26, 62, kRed, "final Texture2D Draw overload flips right half to red");
        checkPixel(39, 62, kBlack, "final Texture2D Draw overload preserves exterior");
        Exit();
    }

public:
    SkiaSpriteBatchRemainingOverloadsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(80);
        gdm_->setPreferredBackBufferHeightProperty(80);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SkiaSpriteBatchRemainingOverloadsTest game;
    game.Run();
    return game.getResult();
}
