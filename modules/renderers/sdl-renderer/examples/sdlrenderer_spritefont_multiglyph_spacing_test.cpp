// SPDX-License-Identifier: MS-PL
// Task 691: Pixel test — draw multiple glyphs with spacing on SDL_Renderer.
//
// Task 425 (this row's own "mirrors" reference, the EasyGL original) is itself not yet
// implemented (still ⬜ in plans/plan_graphics.md's Phase 48) -- there is no existing test to port
// here, same situation as Task 690. This is a NEW SpriteFont pixel test, building on Task 690's
// established single-glyph fixture pattern to additionally exercise the horizontal-advance
// math in the shared, renderer-agnostic SpriteBatch::DrawString: for the 2nd+ glyph in a line,
// `curOffset.X += spacing + cKern.X` (extra inter-character spacing plus the next glyph's own
// left-bearing kerning), and after each glyph, `curOffset.X += cKern.Y + cKern.Z` (that glyph's
// own width plus right-bearing).
//
// Two-glyph font: 'A' (White, 8x8, kerning=(0,8,0)) and 'B' (Green, 8x8, kerning=(0,8,0)),
// spacing=4. Drawing "AB" at position (2,2):
//   'A' (first in line): curOffset.X = abs(0) = 0        -> dest (2, 2, 8, 8)
//   after 'A':           curOffset.X += 8+0 = 8
//   'B' (not first):     curOffset.X += 4+0 = 12         -> dest (14, 2, 8, 8)
// So 'A' occupies screen x in [2,10), 'B' occupies x in [14,22), with an exact 4px gap
// (x in [10,14)) that must stay background -- proving the spacing constant is genuinely
// applied (not zero, not some other value), and that each glyph is placed at its own distinct
// index's colour (ruling out an index mix-up between the two glyphs).
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

class SdlSpriteFontMultiGlyphSpacingTest : public Game
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
                                              /*lineSpacing=*/8, /*spacing=*/4.0f, kerning,
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
        sb_->DrawString(*font_, "AB", Vector2(2.0f, 2.0f), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  6,  6, kWhite, "(6,6) inside 'A' glyph -> White"          },
            { 18,  6, kGreen, "(18,6) inside 'B' glyph -> Green"         },
            { 12,  6, kBlack, "(12,6) gap between glyphs -> Black bg"    },
            {  0,  6, kBlack, "(0,6) left of 'A' -> Black bg"            },
            { 26,  6, kBlack, "(26,6) right of 'B' -> Black bg"          },
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
    SdlSpriteFontMultiGlyphSpacingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteFontMultiGlyphSpacingTest game;
    game.Run();
    return game.getResult();
}
