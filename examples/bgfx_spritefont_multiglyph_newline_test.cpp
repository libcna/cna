// SPDX-License-Identifier: MS-PL
// Task 810: verify SpriteFont draws multiple glyphs with correct spacing AND newline advance on
// Bgfx, combining Task 425's (multi-glyph spacing) and Task 426's (newline) EasyGL/Vulkan tests
// into one Bgfx-specific test, matching this row's own combined scope.
//
// Bgfx-specific adaptation of examples/easygl_spritefont_multiglyph_spacing_test.cpp (Task 425)
// and examples/easygl_spritefont_newline_test.cpp (Task 426), both already reused verbatim on
// Vulkan. Not a verbatim reuse: both files read back several spatially-separate regions from ONE
// rendered frame, incompatible with Bgfx's GetBackBufferData "first read per rendered frame" quirk
// (Task 406) -- restructured into one separately-read RunCheck() pass per region, mirroring Tasks
// 759-809's established Bgfx pattern. Combined into a single font/draw scenario exercising BOTH
// concepts at once (spacing between glyphs on the same line, newline advance between lines),
// rather than 2 separate test binaries, since Task 810's own row description bundles both.
//
// Two-glyph font: 'A' (White, 8x8, kerning=(0,8,0)) and 'B' (Green, 8x8, kerning=(0,8,0)),
// spacing=4, lineSpacing=10 (deliberately different from the glyph's own 8px height, so a bug that
// advances lines by glyph height instead of the font's real lineSpacing_ would be caught).
//
// Drawing "AB\nAB" at position (2,2):
//   Line 1: 'A' (first in line): curOffset.X=0        -> dest (2,  2, 8, 8) -> x in [2,10)
//           'B' (not first):     curOffset.X += 4+0=12 -> dest (14, 2, 8, 8) -> x in [14,22)
//   '\n':   curOffset.X=0; curOffset.Y += 10 -> curOffset=(0,10)
//   Line 2: 'A' (first in line): dest (2,  12, 8, 8) -> y in [12,20)
//           'B' (not first):     dest (14, 12, 8, 8) -> y in [12,20)
// Both lines occupy y in [2,10) and [12,20) respectively, with an exact 2px gap (y in [10,12))
// that must stay background -- proving lineSpacing_ (10), not glyph height (8), is what's applied.
//
// Exit code 0 = all 8 checks PASS, 1 = any FAIL.

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
    const Color kGreen(  0, 255,   0, 255);
    const Color kBlack(  0,   0,   0, 255);
}

class BgfxSpriteFontMultiGlyphNewlineTest : public Game
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
            sb_->DrawString(*font_, "AB\nAB", Vector2(2.0f, 2.0f), Color::White);
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

        // 16x8 atlas: left 8x8 half White ('A'), right 8x8 half Green ('B').
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
                                              /*lineSpacing=*/10, /*spacing=*/4.0f, kerning,
                                              std::optional<SharpRuntime::charcs>(std::nullopt));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  6,  6, kWhite, "(6,6) inside line1 'A' -> White"        },
            { 18,  6, kGreen, "(18,6) inside line1 'B' -> Green"       },
            { 12,  6, kBlack, "(12,6) gap between line1 A/B -> Black"  },
            {  6, 16, kWhite, "(6,16) inside line2 'A' -> White"       },
            { 18, 16, kGreen, "(18,16) inside line2 'B' -> Green"      },
            {  6, 11, kBlack, "(6,11) gap between lines -> Black"      },
            {  0,  6, kBlack, "(0,6) left of line1 'A' -> Black bg"    },
            { 26,  6, kBlack, "(26,6) right of line1 'B' -> Black bg"  },
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

        std::printf("=== %d/8 PASS ===\n", passCount);
        result_ = (passCount == 8) ? 0 : 1;
        Exit();
    }

public:
    BgfxSpriteFontMultiGlyphNewlineTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(24);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteFontMultiGlyphNewlineTest game;
    game.Run();
    return game.getResult();
}
