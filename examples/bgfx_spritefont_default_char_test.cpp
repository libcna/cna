// SPDX-License-Identifier: MS-PL
// Task 811: verify SpriteFont's default character fallback renders the expected glyph on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_spritefont_default_char_test.cpp (Task 427, already
// reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 3 spatially-separate
// regions from ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per
// rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per
// region, mirroring Tasks 759-810's established Bgfx pattern (the no-throw check needs no such
// restructuring since it isn't a pixel read).
//
// Two-glyph font: 'A' (White, 8x8) is the font's only "real" character, '?' (Red, 8x8) is the
// designated defaultCharacter -- but 'Z' is drawn instead, a character that exists in neither the
// input alphabet the font was told about. Since 'Z' is unresolvable, DrawString must fall back to
// rendering '?' 's glyph (Red) at the position 'Z' would have occupied, rather than throwing (a
// defaultCharacter is configured) or silently rendering nothing.
//
// Exit code 0 = all 4 checks PASS, 1 = any FAIL.

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
    const Color kRed  (255,   0,   0, 255);
    const Color kBlack(  0,   0,   0, 255);
}

class BgfxSpriteFontDefaultCharTest : public Game
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
            // 'Z' is not in the font's character list -- must fall back to '?' (Red), not throw.
            sb_->DrawString(*font_, "Z", Vector2(2.0f, 2.0f), Color::White);
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

        bool threw = false;
        try
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin();
            sb_->DrawString(*font_, "Z", Vector2(2.0f, 2.0f), Color::White);
            sb_->End();
        }
        catch (...)
        {
            threw = true;
        }

        int passCount = 0;
        const int totalChecks = 4;

        std::printf("[%s] DrawString(\"Z\") with a configured defaultCharacter does not throw\n",
                    !threw ? "PASS" : "FAIL");
        if (!threw) ++passCount;

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  6,  6, kRed,   "(6,6) inside fallback glyph -> Red ('?', not 'A')" },
            {  0,  0, kBlack, "(0,0) outside glyph -> Black bg"                    },
            { 13, 13, kBlack, "(13,13) outside glyph -> Black bg"                  },
        };

        for (const auto& c : checks)
        {
            const Color got = RunCheck(dev, c.x, c.y);
            const bool ok = colourMatch(got, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            if (ok) ++passCount;
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    BgfxSpriteFontDefaultCharTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteFontDefaultCharTest game;
    game.Run();
    return game.getResult();
}
