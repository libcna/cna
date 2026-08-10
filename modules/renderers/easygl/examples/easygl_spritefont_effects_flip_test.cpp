// SPDX-License-Identifier: MS-PL
// Task 428: Verify SpriteEffects (flip) with DrawString on EasyGL.
//
// EasyGL port of Task 694's SDL_Renderer "Test 1" (the fix itself): Task 694 found and fixed a
// real bug in the SHARED, renderer-agnostic SpriteBatch.cpp (affects every renderer, not just
// SDL_Renderer) -- DrawString previously forwarded `effects` straight to pushSprite() per glyph,
// which only flips that glyph's OWN texture sampling in place; the glyph SEQUENCE/POSITION was
// never mirrored at all, unlike FNA's real algorithm (axisDirectionX/Y + axisIsMirroredX/Y lookup
// tables, MeasureString(text) called up front to shift `origin` by the measured size on the
// mirrored axis). This test independently confirms the fix (already shipped, already verified
// pixel-correct on SDL_Renderer) also renders correctly through EasyGL's own draw renderer.
//
// Two-glyph font: 'A' (White), 'B' (Green), both 8x8. Drawing "AB" with
// SpriteEffects::FlipHorizontally, origin=(0,0), position=(4,4), no rotation/scale.
// MeasureString("AB").X = 16. Correct mirrored result: 'B' now renders FIRST (leftmost, x in
// [4,12)) and 'A' renders SECOND (rightmost, x in [12,20)) -- the exact reverse of the unflipped
// case.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kWhite(255, 255, 255, 255);
static const Color kGreen(  0, 255,   0, 255);
static const Color kBlack(  0,   0,   0, 255);

class EasyGLSpriteFontEffectsFlipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlas_;
    std::unique_ptr<SpriteFont>            font_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // Two-glyph font: 'A' (White), 'B' (Green), both 8x8.
        atlas_ = std::make_unique<Texture2D>(dev, 16, 8);
        std::vector<Color> pixels(128, kBlack);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                pixels[y * 16 + x] = (x < 8) ? kWhite : kGreen;
        atlas_->SetData(pixels.data(), 128);

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A', u'B' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) };
        font_ = std::make_unique<SpriteFont>(*atlas_, glyphBounds, cropping, characters,
                                              /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                                              std::optional<SharpRuntime::charcs>(std::nullopt));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin();
        sb_->DrawString(*font_, "AB", Vector2(4.0f, 4.0f), Color::White, 0.0f,
                         Vector2::Zero, Vector2(1.0f, 1.0f),
                         SpriteEffects::FlipHorizontally, 0.0f);
        sb_->End();

        auto sample = [&](int x, int y) {
            Color px(0, 0, 0, 0);
            Rectangle reg(x, y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        check(colourMatch(sample(8, 8), kGreen),  "FlipHorizontally: 'B' now renders LEFT (x=8) -> Green");
        check(colourMatch(sample(16, 8), kWhite), "FlipHorizontally: 'A' now renders RIGHT (x=16) -> White");

        Exit();
    }

public:
    EasyGLSpriteFontEffectsFlipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLSpriteFontEffectsFlipTest game;
    game.Run();
    return game.getResult();
}
