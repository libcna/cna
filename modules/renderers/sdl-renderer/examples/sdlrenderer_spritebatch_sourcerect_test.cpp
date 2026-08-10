// SPDX-License-Identifier: MS-PL
// Task 673: SDL_Renderer SpriteBatch source rectangle cropping pixel test.
//
// Direct port of Task 419's EasyGL test (examples/easygl_spritebatch_sourcerect_test.cpp) -- same
// methodology, same geometry, same expected pixel outcomes. See that file's header comment for
// the full rationale. Unlike the SpriteSortMode/scale overload tasks (667-670, 672), source
// rectangle cropping IS genuinely renderer-specific: confirmed by reading
// SdlSpriteBatchRenderer::Draw() directly, it passes sourceRectangle straight through as
// SDL_RenderTexture()'s own `srcrect` parameter, so this is real GPU-dispatch-level behaviour to
// verify, not just shared SpriteBatch.cpp logic.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
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
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 60)
{
    return std::abs((int)got.getRProperty()  - (int)want.getRProperty())  <= tol
        && std::abs((int)got.getGProperty()  - (int)want.getGProperty())  <= tol
        && std::abs((int)got.getBProperty()  - (int)want.getBProperty())  <= tol;
}

static const Color kRed     (255,   0,   0, 255);
static const Color kBlue    (  0,   0, 255, 255);
static const Color kMagenta (255,   0, 255, 255);
static const Color kYellow  (255, 255,   0, 255);
static const Color kGreen   (  0, 255,   0, 255);

class SdlSpriteBatchSourceRectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

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

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 105, 105, kBlue,  "Near dest top-left -> Blue (cropped, not whole-texture top-left Red)" },
            { 145, 145, kBlue,  "Near dest bottom-right -> Blue (not whole-texture bottom-right Yellow)" },
            { 200,  50, kGreen, "Outside sprite entirely -> Green background" },
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
    SdlSpriteBatchSourceRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(200);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteBatchSourceRectTest game;
    game.Run();
    return game.getResult();
}
