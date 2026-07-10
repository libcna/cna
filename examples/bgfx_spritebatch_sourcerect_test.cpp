// SPDX-License-Identifier: MS-PL
// Task 806: verify SpriteBatch::Draw's sourceRectangle parameter correctly crops which region of
// the texture is sampled on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_spritebatch_sourcerect_test.cpp (Task 419, already
// reused verbatim on Vulkan). Not a verbatim reuse: that file reads back 3 spatially-separate
// regions from ONE rendered frame, incompatible with Bgfx's GetBackBufferData "first read per
// rendered frame" quirk (Task 406) -- restructured into one separately-read RunCheck() pass per
// region, mirroring Tasks 759-805's established Bgfx pattern.
//
// Design: a 20x20 texture split into a 2x2 grid of 10x10 solid-color cells:
//   top-left     (0,0)-(10,10)   = Red
//   top-right    (10,0)-(20,10)  = Blue
//   bottom-left  (0,10)-(10,20)  = Magenta
//   bottom-right (10,10)-(20,20) = Yellow
//
// Drawn with sourceRectangle=(10,0,10,10) -- the top-right (Blue) cell only -- stretched into a
// 50x50 destinationRectangle at (100,100). If cropping works correctly, the ENTIRE drawn sprite
// must be uniformly Blue, since only that one solid-color cell was selected.
//
// If sourceRectangle were ignored (a bug that stretches the WHOLE 20x20 texture into the
// destination instead of just the selected cell), sampling near the destination rectangle's
// top-left corner would show the texture's own top-left cell (Red) instead of Blue, and sampling
// near the bottom-right corner would show the texture's own bottom-right cell (Yellow) instead
// of Blue -- 2 independent, differently-colored tells for the same "no cropping" bug.
//
// Exit code 0 = all 3 checks PASS, 1 = any FAIL.

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

    const Color kRed     (255,   0,   0, 255);
    const Color kBlue    (  0,   0, 255, 255);
    const Color kMagenta (255,   0, 255, 255);
    const Color kYellow  (255, 255,   0, 255);
    const Color kGreen   (  0, 255,   0, 255);
}

class BgfxSpriteBatchSourceRectTest : public Game
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

            // sourceRectangle selects only the top-right (Blue) cell; stretched to 50x50.
            sb_->Draw(*tex_,
                      Rectangle(100, 100, 50, 50),  // destinationRectangle
                      Rectangle(10, 0, 10, 10),      // sourceRectangle: top-right cell only
                      Color::White);

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

        // 20x20, 2x2 grid of 10x10 solid-color cells.
        tex_ = std::make_unique<Texture2D>(dev, 20, 20);
        std::vector<Color> pixels(20 * 20, kRed);
        for (int y = 0; y < 20; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                const bool right  = x >= 10;
                const bool bottom = y >= 10;
                Color c = kRed;
                if (!bottom && right)  c = kBlue;
                if (bottom  && !right) c = kMagenta;
                if (bottom  && right)  c = kYellow;
                pixels[y * 20 + x] = c;
            }
        }
        tex_->SetData(pixels.data(), static_cast<int>(pixels.size()));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 105, 105, kBlue,  "Near dest top-left -> Blue (cropped, not whole-texture top-left Red)" },
            { 145, 145, kBlue,  "Near dest bottom-right -> Blue (not whole-texture bottom-right Yellow)" },
            { 200,  50, kGreen, "Outside sprite entirely -> Green background" },
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

        std::printf("=== %d/3 PASS ===\n", passCount);
        result_ = (passCount == 3) ? 0 : 1;
        Exit();
    }

public:
    BgfxSpriteBatchSourceRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteBatchSourceRectTest game;
    game.Run();
    return game.getResult();
}
