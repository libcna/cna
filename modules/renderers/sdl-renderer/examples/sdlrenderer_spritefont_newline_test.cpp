// SPDX-License-Identifier: MS-PL
// Task 692: Pixel test — newline advances by line spacing on SDL_Renderer.
//
// Task 426 (this row's own "mirrors" reference, the EasyGL original) is itself not yet
// implemented (still ⬜ in plans/plan_graphics.md's Phase 48) -- there is no existing test to port
// here, same situation as Tasks 690/691. This is a NEW SpriteFont pixel test, building on
// Task 690's fixture pattern to exercise the newline branch of the shared, renderer-agnostic
// SpriteBatch::DrawString: `curOffset.X = 0; curOffset.Y += spriteFont.lineSpacing_;
// firstInLine = true;`.
//
// Single-glyph font: 'A' (White, 8x8, kerning=(0,8,0)), lineSpacing=10 -- deliberately
// different from the glyph's own 8px height, so a bug that advances by glyph height instead
// of the font's real lineSpacing_ would be caught (not just coincidentally pass). Drawing
// "A\nA" at position (2,2):
//   1st 'A' (first in line): dest (2, 2, 8, 8)                    -> occupies y in [2,10)
//   '\n': curOffset.Y += 10 -> curOffset = (0, 10)
//   2nd 'A' (first in line again): dest (2, 12, 8, 8)             -> occupies y in [12,20)
// Leaving an exact 2px gap (y in [10,12)) between the two lines that must stay background.
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
static const Color kBlack(  0,   0,   0, 255);

class SdlSpriteFontNewlineTest : public Game
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

        atlas_ = std::make_unique<Texture2D>(dev, 8, 8);
        std::vector<Color> whitePixels(64, kWhite);
        atlas_->SetData(whitePixels.data(), 64);

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f) };
        font_ = std::make_unique<SpriteFont>(*atlas_, glyphBounds, cropping, characters,
                                              /*lineSpacing=*/10, /*spacing=*/0.0f, kerning,
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
        sb_->DrawString(*font_, "A\nA", Vector2(2.0f, 2.0f), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 6,  6, kWhite, "(6,6) inside 1st-line glyph -> White"     },
            { 6, 16, kWhite, "(6,16) inside 2nd-line glyph -> White"    },
            { 6, 11, kBlack, "(6,11) gap between lines -> Black bg"     },
            { 6, 22, kBlack, "(6,22) below 2nd-line glyph -> Black bg"  },
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
    SdlSpriteFontNewlineTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(24);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteFontNewlineTest game;
    game.Run();
    return game.getResult();
}
