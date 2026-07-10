// SPDX-License-Identifier: MS-PL
// Task 807: verify SpriteEffects::FlipHorizontally/FlipVertically on Bgfx (mirrors Task 167).
//
// Bgfx-specific adaptation of examples/easygl_sprite_effects_test.cpp (Task 167, already reused
// verbatim on Vulkan). Not a verbatim reuse: that file reads back 8 spatially-separate points from
// 4 draws in ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per
// rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per
// check, mirroring Tasks 759-806's established Bgfx pattern (each check redoes the relevant single
// draw for that section).
//
// Design:
//   Section 0 (x=0..99):    2x1 texture [Red|Blue], no flip     -> left half Red, right half Blue
//   Section 1 (x=100..199): same texture, FlipHorizontally      -> left half Blue, right half Red
//   Section 2 (x=200..299): 1x2 texture [Red / Blue], no flip   -> top half Red, bottom half Blue
//   Section 3 (x=300..399): same texture, FlipVertically        -> top half Blue, bottom half Red
//
// SamplerState::PointClamp (nearest-neighbour) is used so every pixel maps to exactly one texel --
// no bilinear ambiguity.
//
// Exit code 0 = all 8 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool colourMatch(Color got, Color want, int tol = 60)
    {
        return std::abs((int)got.getRProperty()  - (int)want.getRProperty())  <= tol
            && std::abs((int)got.getGProperty()  - (int)want.getGProperty())  <= tol
            && std::abs((int)got.getBProperty()  - (int)want.getBProperty())  <= tol;
    }

    const Color kRed  (255,   0,   0, 255);
    const Color kBlue (  0,   0, 255, 255);
}

class BgfxSpriteEffectsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    // 2x1: [Red | Blue]  (column 0 = Red, column 1 = Blue)
    std::unique_ptr<Texture2D> hTex_;
    // 1x2: [Red / Blue]  (row 0 = Red, row 1 = Blue)
    std::unique_ptr<Texture2D> vTex_;

    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, Texture2D& tex, const Rectangle& dest,
                    const Rectangle& src, SpriteEffects effects, int x, int y)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);

            sb_->Begin(SpriteSortMode::Deferred,
                       BlendState::Opaque,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

            sb_->Draw(tex, dest, src, Color::White, 0.0f, Vector2::Zero, effects, 0.0f);

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

        hTex_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color hPix[2] = { kRed, kBlue };
        hTex_->SetData(hPix, 2);

        vTex_ = std::make_unique<Texture2D>(dev, 1, 2);
        const Color vPix[2] = { kRed, kBlue };
        vTex_->SetData(vPix, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        const Rectangle hFull(0, 0, 2, 1);
        const Rectangle vFull(0, 0, 1, 2);

        struct Check {
            Texture2D* tex; Rectangle dest; Rectangle src; SpriteEffects effects;
            int x; int y; Color want; const char* label;
        };
        const Check checks[] = {
            { hTex_.get(), Rectangle(0,   0, 100, 100), hFull, SpriteEffects::None,           25, 50, kRed,  "S0-left:  NoFlip   -> Red"  },
            { hTex_.get(), Rectangle(0,   0, 100, 100), hFull, SpriteEffects::None,           75, 50, kBlue, "S0-right: NoFlip   -> Blue" },
            { hTex_.get(), Rectangle(100, 0, 100, 100), hFull, SpriteEffects::FlipHorizontally, 125, 50, kBlue, "S1-left:  FlipH    -> Blue" },
            { hTex_.get(), Rectangle(100, 0, 100, 100), hFull, SpriteEffects::FlipHorizontally, 175, 50, kRed,  "S1-right: FlipH    -> Red"  },
            { vTex_.get(), Rectangle(200, 0, 100, 100), vFull, SpriteEffects::None,           250, 25, kRed,  "S2-top:   NoFlip   -> Red"  },
            { vTex_.get(), Rectangle(200, 0, 100, 100), vFull, SpriteEffects::None,           250, 75, kBlue, "S2-bot:   NoFlip   -> Blue" },
            { vTex_.get(), Rectangle(300, 0, 100, 100), vFull, SpriteEffects::FlipVertically,   350, 25, kBlue, "S3-top:   FlipV    -> Blue" },
            { vTex_.get(), Rectangle(300, 0, 100, 100), vFull, SpriteEffects::FlipVertically,   350, 75, kRed,  "S3-bot:   FlipV    -> Red"  },
        };

        int passCount = 0;
        for (const auto& c : checks)
        {
            const Color got = RunCheck(dev, *c.tex, c.dest, c.src, c.effects, c.x, c.y);
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
    BgfxSpriteEffectsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(100);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteEffectsTest game;
    game.Run();
    return game.getResult();
}
