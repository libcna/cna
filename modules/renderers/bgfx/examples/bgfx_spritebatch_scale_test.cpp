// SPDX-License-Identifier: MS-PL
// Task 805: verify SpriteBatch::Draw's scalar and Vector2 scale overloads on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_spritebatch_scale_test.cpp (Task 418, already reused
// verbatim on Vulkan). Not a verbatim reuse: that file reads back 5 spatially-separate regions
// from ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per rendered
// frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per region,
// mirroring Tasks 759-804's established Bgfx pattern (each check redoes the full
// Clear+Begin/Draw/Draw/End sequence).
//
// Design: a single 20x20 solid-Red texture, drawn twice against a Green background.
//
//   Draw 1 (scalar overload): position=(50,50), scale=3.0f -> expected 60x60 destination
//     rectangle spanning screen x:[50,110), y:[50,110).
//   Draw 2 (Vector2 overload): position=(200,50), scale=(2.0f,4.0f) (non-uniform) -> expected
//     40x80 destination rectangle spanning screen x:[200,240), y:[50,130).
//
// Check points for Draw 2 are chosen to discriminate a correct non-uniform implementation from
// 2 plausible bugs: using only scale.X for both axes (would give 40x40, edge y=90) and using
// only scale.Y for both axes (would give 80x80, edge x=280).
//
// Exit code 0 = all 5 checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

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

    const Color kRed   (255,   0,   0, 255);
    const Color kGreen (  0, 255,   0, 255);
}

class BgfxSpriteBatchScaleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int x, int y)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kGreen);
            dev.setBlendStateProperty(BlendState::Opaque);

            sb_->Begin(SpriteSortMode::Deferred,
                       BlendState::Opaque,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

            // Scalar scale overload: 20x20 source * 3.0f -> 60x60 destination.
            sb_->Draw(*tex_, Vector2(50.0f, 50.0f), std::optional<Rectangle>{}, Color::White,
                      0.0f, Vector2::Zero, 3.0f, SpriteEffects::None, 0.0f);

            // Vector2 (non-uniform) scale overload: 20x20 source * (2,4) -> 40x80 destination.
            sb_->Draw(*tex_, Vector2(200.0f, 50.0f), std::optional<Rectangle>{}, Color::White,
                      0.0f, Vector2::Zero, Vector2(2.0f, 4.0f), SpriteEffects::None, 0.0f);

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

        tex_ = std::make_unique<Texture2D>(dev, 20, 20);
        std::vector<Color> pixels(20 * 20, kRed);
        tex_->SetData(pixels.data(), static_cast<int>(pixels.size()));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 100, 100, kRed,   "Scalar scale (3.0): interior -> Red" },
            { 115,  80, kGreen, "Scalar scale (3.0): just past right edge -> Green" },
            { 220, 110, kRed,   "Vector2 scale (2,4): interior -> Red (rules out X-for-both bug)" },
            { 245,  70, kGreen, "Vector2 scale (2,4): just past right edge -> Green (rules out Y-for-both bug)" },
            {  50, 250, kGreen, "Far from both sprites -> Green background" },
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
    BgfxSpriteBatchScaleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteBatchScaleTest game;
    game.Run();
    return game.getResult();
}
