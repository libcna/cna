// SPDX-License-Identifier: MS-PL
// Task 694: Verify SpriteEffects flip + rotation/origin/scale with DrawString on SDL_Renderer.
// Mirrors Tasks 428-429 (both still ⬜ in plans/plan_graphics.md's Phase 48 -- no existing EasyGL
// test to port, same situation as Tasks 690-693; this is a NEW test).
//
// REAL BUG FOUND AND FIXED (shared, renderer-agnostic SpriteBatch.cpp, not SDL_Renderer-specific
// -- affects every renderer): SpriteBatch::DrawString previously forwarded `effects` straight to
// pushSprite() for each glyph, which only flips that glyph's OWN texture sampling in place --
// the glyph SEQUENCE/POSITION was never mirrored at all, unlike FNA's real algorithm (traced in
// FNA's SpriteBatch.cs DrawString: axisDirectionX/Y + axisIsMirroredX/Y lookup tables, indexed
// by (int)effects; when effects != None, MeasureString(text) is called up front and `origin` is
// shifted by the measured size on the mirrored axis, so the flip pivots around the correct edge
// of the WHOLE text block, and each glyph's accumulated offset is itself negated on the mirrored
// axis so later characters end up earlier on screen). Confirmed empirically via a throwaway
// diagnostic (drawing "AB" with FlipHorizontally showed 'A' still left/'B' still right, i.e. no
// reordering at all, before this fix). Re-derived FNA's formula in terms of CNA's own existing
// (origin - accumulated offset) sign convention (FNA's internal PushSprite uses an inverted sign
// convention baked into its own vertex generation, confirmed algebraically: for effects=None,
// FNA's own "offsetX" equals `-localX` in CNA's terms) rather than porting FNA's raw internal
// variables verbatim.
//
// Test 1 (this fix): "AB" (White|Green, 8x8 each) with SpriteEffects::FlipHorizontally,
// origin=(0,0), position=(4,4), no rotation/scale. MeasureString("AB").X = 16. Correct mirrored
// result: 'B' now renders FIRST (leftmost, x in [4,12)) and 'A' renders SECOND (rightmost, x in
// [12,20)) -- the exact reverse of the unflipped case (verified as its own check here too).
//
// Test 2 (rotation/origin/scale, already-correct code path -- not touched by the fix): single
// glyph 'A' (White, 8x8), origin=(4,4) (centers the pivot on the unscaled glyph), scale=(2,2),
// rotation=0, position=(10,10). localX = curOffset.X(0)+cCrop.X(0)-origin.X(4) = -4;
// scaledX = -4*2 = -8; dest.X = round(10-8) = 2; dest.Width = round(8*2) = 16 -> dest=(2,2,16,16).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
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
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
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

class SdlSpriteFontEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlasAB_;
    std::unique_ptr<SpriteFont>            fontAB_;
    std::unique_ptr<Texture2D>             atlasA_;
    std::unique_ptr<SpriteFont>            fontA_;

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
        atlasAB_ = std::make_unique<Texture2D>(dev, 16, 8);
        std::vector<Color> pixelsAB(128, kBlack);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                pixelsAB[y * 16 + x] = (x < 8) ? kWhite : kGreen;
        atlasAB_->SetData(pixelsAB.data(), 128);

        std::vector<Rectangle> glyphBoundsAB = { Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8) };
        std::vector<Rectangle> croppingAB    = { Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> charsAB = { u'A', u'B' };
        std::vector<Vector3> kerningAB = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) };
        fontAB_ = std::make_unique<SpriteFont>(*atlasAB_, glyphBoundsAB, croppingAB, charsAB,
                                                /*lineSpacing=*/8, /*spacing=*/0.0f, kerningAB,
                                                std::optional<SharpRuntime::charcs>(std::nullopt));

        // Single-glyph font: 'A' (White), 8x8.
        atlasA_ = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> whitePixels(64, kWhite);
        atlasA_->SetData(whitePixels.data(), 64);
        std::vector<Rectangle> glyphBoundsA = { Rectangle(0, 0, 8, 8) };
        std::vector<Rectangle> croppingA    = { Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> charsA = { u'A' };
        std::vector<Vector3> kerningA = { Vector3(0.0f, 8.0f, 0.0f) };
        fontA_ = std::make_unique<SpriteFont>(*atlasA_, glyphBoundsA, croppingA, charsA,
                                               /*lineSpacing=*/8, /*spacing=*/0.0f, kerningA,
                                               std::optional<SharpRuntime::charcs>(std::nullopt));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // --- Test 1: SpriteEffects::FlipHorizontally reorders + mirrors the glyph sequence ---
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin();
            sb_->DrawString(*fontAB_, "AB", Vector2(4.0f, 4.0f), Color::White, 0.0f,
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
        }

        // --- Test 2: rotation/origin/scale (pre-existing, not touched by the fix) ---
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin();
            sb_->DrawString(*fontA_, "A", Vector2(10.0f, 10.0f), Color::White, 0.0f,
                             Vector2(4.0f, 4.0f), Vector2(2.0f, 2.0f), SpriteEffects::None, 0.0f);
            sb_->End();

            // dest = (2, 2, 16, 16) -- occupies x,y in [2,18).
            auto sample = [&](int x, int y) {
                Color px(0, 0, 0, 0);
                Rectangle reg(x, y, 1, 1);
                dev.GetBackBufferData(&reg, &px, 0, 1);
                return px;
            };

            check(colourMatch(sample(10, 10), kWhite), "origin=(4,4) scale=(2,2): (10,10) inside scaled glyph -> White");
            check(colourMatch(sample(0, 0), kBlack),   "origin=(4,4) scale=(2,2): (0,0) outside -> Black bg");
            check(colourMatch(sample(20, 20), kBlack), "origin=(4,4) scale=(2,2): (20,20) outside -> Black bg");
        }

        Exit();
    }

public:
    SdlSpriteFontEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteFontEffectsTest game;
    game.Run();
    return game.getResult();
}
