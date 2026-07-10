// SPDX-License-Identifier: MS-PL
// Task 809: verify SpriteFont draws a single glyph at a known position on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_spritefont_single_glyph_test.cpp (Task 424, already
// reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 5 spatially-separate
// regions from ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per
// rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per
// region, mirroring Tasks 759-808's established Bgfx pattern.
//
// Since CNA has no XNB content pipeline, SpriteFont exposes its raw glyph/cropping/kerning tables
// directly -- this test hand-builds a minimal, single-character font: an 8x8 solid-white atlas
// texture representing the single glyph 'A', with zero cropping offset and zero left/right
// kerning bearing, so the destination rect maps directly and exactly:
//   glyphBounds = cropping = (0, 0, 8, 8);  kerning = (leftBearing=0, width=8, rightBearing=0)
// Drawing "A" at position (4,4) with default origin/scale/rotation should place the glyph at
// exactly screen rect (4,4,8,8) on a black background.
//
// Checks a point INSIDE the glyph rect (must be white), and 4 points immediately OUTSIDE each of
// the glyph rect's 4 edges, at that edge's own midpoint -- edge-midpoint checks independently
// discriminate an X-only or Y-only placement error.
//
// Exit code 0 = all 5 checks PASS, 1 = any FAIL.

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

namespace
{
    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }

    const Color kWhite(255, 255, 255, 255);
    const Color kBlack(  0,   0,   0, 255);
}

class BgfxSpriteFontSingleGlyphTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlas_;
    std::unique_ptr<SpriteFont>            font_;

    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int x, int y)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);

            sb_->Begin();
            sb_->DrawString(*font_, "A", Vector2(4.0f, 4.0f), Color::White);
            sb_->End();

            const Rectangle reg(x, y, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

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

        // Glyph occupies screen rect (4,4,8,8) -> x in [4,12), y in [4,12).
        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  8,  8, kWhite, "(8,8) inside glyph rect -> White"                    },
            {  3,  8, kBlack, "(3,8) just left of left edge (mid-height) -> Black"  },
            { 12,  8, kBlack, "(12,8) just right of right edge (mid-height) -> Black" },
            {  8,  3, kBlack, "(8,3) just above top edge (mid-width) -> Black"      },
            {  8, 12, kBlack, "(8,12) just below bottom edge (mid-width) -> Black"  },
        };

        int passCount = 0;
        for (const auto& c : checks)
        {
            const Color got = RunCheck(dev, c.x, c.y);
            const bool ok = colourMatch(got, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            if (ok) ++passCount;
        }

        std::printf("=== %d/5 PASS ===\n", passCount);
        result_ = (passCount == 5) ? 0 : 1;
        Exit();
    }

public:
    BgfxSpriteFontSingleGlyphTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteFontSingleGlyphTest game;
    game.Run();
    return game.getResult();
}
