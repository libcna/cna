// SPDX-License-Identifier: MS-PL
// Task 730 (sample 4 of 5): minimal 2D-only FNA/XNA sample -- a classic "display text via
// SpriteFont" demo, specifically targeting SDL_Renderer. Unlike Task 690's single-glyph pixel
// test, this draws a genuine two-character STRING ("HI"), proving multi-glyph string layout
// (kerning-driven horizontal advance placing the second glyph immediately after the first, not
// just one enlarged glyph or overlapping glyphs) renders correctly on this renderer.
//
// Since CNA has no XNB content pipeline, SpriteFont exposes its raw glyph/cropping/kerning
// tables directly (matches Task 690's established "hand-build a minimal fixture" convention):
// a 16x8 solid-white atlas holding two adjacent 8x8 glyphs, 'H' at (0,0,8,8) and 'I' at (8,0,8,8),
// zero cropping offset, zero left/right kerning bearing on both. Drawing "HI" at (2,2) should
// place 'H' at screen rect (2,2,8,8) and 'I' immediately after at (10,2,8,8).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding), matching every other
// SDL_Renderer pixel-verification test in this project.

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

namespace
{
    const Color kWhite(255, 255, 255, 255);
    const Color kBlack(  0,   0,   0, 255);

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }
}

class SdlSpriteFontTextSample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             atlas_;
    std::unique_ptr<SpriteFont>            font_;

    int pass_ = 0, fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        // 16x8 solid-white atlas holding two adjacent 8x8 glyphs: 'H' then 'I'.
        atlas_ = std::make_unique<Texture2D>(dev, 16, 8);
        std::vector<Color> whitePixels(128, kWhite);
        atlas_->SetData(whitePixels.data(), 128);

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'H', u'I' };
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
        sb_->DrawString(*font_, "HI", Vector2(2.0f, 2.0f), Color::White);
        sb_->End();

        struct CheckPt { int x; int y; Color want; const char* label; };
        const CheckPt checks[] = {
            {  6,  6, kWhite, "(6,6) inside 'H' glyph rect -> White"          },
            { 14,  6, kWhite, "(14,6) inside 'I' glyph rect -> White"        },
            {  1,  1, kBlack, "(1,1) above-left of the string -> Black bg"   },
            { 19,  1, kBlack, "(19,1) beyond the right of 'I' -> Black bg"   },
            {  1, 11, kBlack, "(1,11) below the string -> Black bg"         },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            check(colourMatch(px, c.want), c.label);
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlSpriteFontTextSample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(24);
        gdm_->setPreferredBackBufferHeightProperty(24);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlSpriteFontTextSample game;
    game.Run();
    return game.getResult();
}
