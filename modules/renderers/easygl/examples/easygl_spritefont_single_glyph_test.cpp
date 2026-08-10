// SPDX-License-Identifier: MS-PL
// Task 424: Pixel test -- draw a single glyph at a known position on EasyGL.
//
// Direct EasyGL port of Task 690's SDL_Renderer test (which itself was the first-of-its-kind
// SpriteFont pixel test in this project, designed directly from FNA's real SpriteBatch::DrawString
// contract -- traced in the shared, renderer-agnostic SpriteBatch.cpp: per-glyph destination rect
// = position + accumulated kerning/cropping offset, sized by the glyph's own bounds, sampling the
// glyph's source rect from the font's atlas texture).
//
// Since CNA has no XNB content pipeline, SpriteFont exposes its raw glyph/cropping/kerning tables
// directly (see SpriteFont.hpp) -- this test hand-builds a minimal, single-character font,
// matching this project's established "hand-build a minimal fixture when no real asset is
// available" convention: an 8x8 solid-white atlas texture representing the single glyph 'A', with
// zero cropping offset and zero left/right kerning bearing, so the destination rect maps directly
// and exactly:
//   glyphBounds = cropping = (0, 0, 8, 8);  kerning = (leftBearing=0, width=8, rightBearing=0)
// Drawing "A" at position (4,4) with default origin/scale/rotation should place the glyph at
// exactly screen rect (4,4,8,8) on a black background.
//
// Checks a point INSIDE the glyph rect (must be white), and 4 points immediately OUTSIDE each
// of the glyph rect's 4 edges, at that edge's own midpoint -- not the diagonal corners, since a
// pure horizontal (or vertical) mis-placement wouldn't reach a corner check that is ALSO offset
// on the other axis, silently passing a broken test. Edge-midpoint checks independently
// discriminate an X-only or Y-only placement error.
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
static const Color kBlack(  0,   0,   0, 255);

class EasyGLSpriteFontSingleGlyphTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlas_;
    std::unique_ptr<SpriteFont>            font_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // 8x8 solid-white glyph atlas (single glyph 'A' fills the whole texture).
        atlas_ = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> whitePixels(64, kWhite);
        atlas_->SetData(whitePixels.data(), 64);

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f) };
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
        sb_->DrawString(*font_, "A", Vector2(4.0f, 4.0f), Color::White);
        sb_->End();

        // Glyph occupies screen rect (4,4,8,8) -> x in [4,12), y in [4,12).
        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  8,  8, kWhite, "(8,8) inside glyph rect -> White"                    },
            {  3,  8, kBlack, "(3,8) just left of left edge (mid-height) -> Black"  },
            { 12,  8, kBlack, "(12,8) just right of right edge (mid-height) -> Black" },
            {  8,  3, kBlack, "(8,3) just above top edge (mid-width) -> Black"      },
            {  8, 12, kBlack, "(8,12) just below bottom edge (mid-width) -> Black"  },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            const bool ok = colourMatch(px, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    EasyGLSpriteFontSingleGlyphTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLSpriteFontSingleGlyphTest game;
    game.Run();
    return game.getResult();
}
