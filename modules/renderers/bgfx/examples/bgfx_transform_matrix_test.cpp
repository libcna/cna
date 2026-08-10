// SPDX-License-Identifier: MS-PL
// Task 808: verify SpriteBatch::Begin's transformMatrix parameter on Bgfx (mirrors Task 168).
//
// Bgfx-specific adaptation of examples/easygl_transform_matrix_test.cpp (Task 168, already reused
// verbatim on Vulkan). Not a verbatim reuse: that file reads back 2 spatially-separate regions
// from ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per rendered
// frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per region,
// mirroring Tasks 759-807's established Bgfx pattern.
//
// Design:
//   Viewport 400x200, black background.
//   A 1x1 red texture is drawn at position (0,0) with a translation matrix
//   Matrix::CreateTranslation(100, 50, 0). The sprite should appear at (100,50).
//
//   Readback:
//     (0,   0)  -> Black  (original position, sprite was moved away)
//     (100, 50) -> Red    (translated position)
//
// Exit code 0 = all 2 checks PASS, 1 = any FAIL.

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
    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }

    const Color kBlack(  0,   0,   0, 255);
    const Color kRed  (255,   0,   0, 255);
}

class BgfxTransformMatrixTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;

    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int x, int y)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);

            // Translate every sprite by (+100, +50).
            Matrix tx = Matrix::CreateTranslation(100.0f, 50.0f, 0.0f);

            sb_->Begin(SpriteSortMode::Deferred,
                       BlendState::Opaque,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr, nullptr, tx);

            // Draw the 1x1 red sprite at (0,0) -- transform moves it to (100,50).
            sb_->Draw(*redTex_, Vector2(0.0f, 0.0f), Color::White);

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
        sb_      = std::make_unique<SpriteBatch>(dev);
        redTex_  = std::make_unique<Texture2D>(dev, 1, 1);
        const Color px = kRed;
        redTex_->SetData(&px, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {   0,  0, kBlack, "(0,0):   origin -- should be Black (sprite was translated away)" },
            { 100, 50, kRed,   "(100,50): translated position -- should be Red"                  },
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

        std::printf("=== %d/2 PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    BgfxTransformMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxTransformMatrixTest game;
    game.Run();
    return game.getResult();
}
