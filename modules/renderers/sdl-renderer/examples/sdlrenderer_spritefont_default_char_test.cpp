// SPDX-License-Identifier: MS-PL
// Task 693: Pixel test — default character fallback renders the expected glyph on SDL_Renderer.
//
// Task 427 (this row's own "mirrors" reference, the EasyGL original) is itself not yet
// implemented (still ⬜ in plans/plan_graphics.md's Phase 48) -- there is no existing test to port
// here, same situation as Tasks 690-692. This is a NEW SpriteFont pixel test, building on
// Task 690's fixture pattern to exercise the shared, renderer-agnostic SpriteBatch::DrawString's
// unknown-character fallback branch:
//   auto it = spriteFont.characterIndexMap_.find(c);
//   if (it == spriteFont.characterIndexMap_.end())
//   {
//       if (!spriteFont.defaultCharacter_.has_value()) throw ...;
//       it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());
//   }
//
// Two-glyph font: 'A' (White, 8x8) is the font's only "real" character, '?' (Red, 8x8) is the
// designated defaultCharacter -- but 'Z' is drawn instead, a character that exists in neither
// the input alphabet the font was told about... i.e. it is NOT in the character list at all.
// Since 'Z' is unresolvable, DrawString must fall back to rendering '?' 's glyph (Red) at the
// position 'Z' would have occupied, rather than throwing (a defaultCharacter is configured) or
// silently rendering nothing (a would-be "must not silently misrender" gap, mirroring this
// project's own established Task 675/676/681 "must not silently no-op/misrender" convention).
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
static const Color kRed  (255,   0,   0, 255);
static const Color kBlack(  0,   0,   0, 255);

class SdlSpriteFontDefaultCharTest : public Game
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

        // 16x8 atlas: left 8x8 half White ('A'), right 8x8 half Red ('?', the default char).
        atlas_ = std::make_unique<Texture2D>(dev, 16, 8);
        std::vector<Color> pixels(128, kBlack);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                pixels[y * 16 + x] = (x < 8) ? kWhite : kRed;
        atlas_->SetData(pixels.data(), 128);

        std::vector<Rectangle> glyphBounds = { Rectangle(0, 0, 8, 8), Rectangle(8, 0, 8, 8) };
        std::vector<Rectangle> cropping    = { Rectangle(0, 0, 8, 8), Rectangle(0, 0, 8, 8) };
        std::vector<SharpRuntime::charcs> characters = { u'A', u'?' };
        std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) };
        font_ = std::make_unique<SpriteFont>(*atlas_, glyphBounds, cropping, characters,
                                              /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                                              std::optional<SharpRuntime::charcs>(u'?'));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        bool threw = false;
        try
        {
            sb_->Begin();
            // 'Z' is not in the font's character list -- must fall back to '?' (Red), not throw.
            sb_->DrawString(*font_, "Z", Vector2(2.0f, 2.0f), Color::White);
            sb_->End();
        }
        catch (...)
        {
            threw = true;
        }

        std::printf("[%s] DrawString(\"Z\") with a configured defaultCharacter does not throw\n",
                    !threw ? "PASS" : "FAIL");
        if (threw) result_ = 1;

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  6,  6, kRed,   "(6,6) inside fallback glyph -> Red ('?', not 'A')" },
            {  0,  0, kBlack, "(0,0) outside glyph -> Black bg"                    },
            { 13, 13, kBlack, "(13,13) outside glyph -> Black bg"                  },
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
    SdlSpriteFontDefaultCharTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteFontDefaultCharTest game;
    game.Run();
    return game.getResult();
}
