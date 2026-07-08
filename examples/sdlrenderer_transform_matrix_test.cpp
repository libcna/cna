// SPDX-License-Identifier: MS-PL
// Task 675: SDL_Renderer SpriteBatch::Begin transformMatrix pixel integration test.
//
// Direct port of Task 168's EasyGL test (examples/easygl_transform_matrix_test.cpp) -- same
// methodology, same geometry, same expected pixel outcomes.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Design:
//   Viewport 400x200, black background.
//   A 1x1 red texture is drawn at position (0,0) with a translation matrix
//   Matrix::CreateTranslation(100, 50, 0).  The sprite should appear at (100,50).
//
//   Readback:
//     (0,   0)  -> Black  (original position, sprite was moved away)
//     (100, 50) -> Red    (translated position)
//
// A second sprite exercises the SDL_RenderTextureAffine path's flip-corner-permutation logic
// combined with a transform, not just plain translation: a 2x1 [Red|Blue] texture drawn at
// destRect (0,100,40,20) with SpriteEffects::FlipHorizontally, under the same translate-by-
// (100,50) transform. Without the flip, the left half would be Red and the right half Blue;
// FlipHorizontally swaps that. After the (+100,+50) translation the sprite occupies screen
// x:[100,140), y:[150,170) -- (110,160) must read Blue (flipped left half) and (130,160) must
// read Red (flipped right half). Positioned well away from the first sprite's (100,50) check
// point so the two sprites' regions don't overlap.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kBlack(  0,   0,   0, 255);
static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);

class SdlTransformMatrixTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;
    std::unique_ptr<Texture2D>             hTex_; // 2x1: [Red | Blue]

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_      = std::make_unique<SpriteBatch>(dev);
        redTex_  = std::make_unique<Texture2D>(dev, 1, 1);
        const Color px = kRed;
        redTex_->SetData(&px, 1);

        hTex_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color hPix[2] = { kRed, kBlue };
        hTex_->SetData(hPix, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Translate every sprite by (+100, +50).
        Matrix tx = Matrix::CreateTranslation(100.0f, 50.0f, 0.0f);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, tx);

        // Draw the 1x1 red sprite at (0,0) -- transform moves it to (100,50).
        sb_->Draw(*redTex_, Vector2(0.0f, 0.0f), Color::White);

        // FlipHorizontally + transform combined (exercises the affine path's corner-permutation
        // logic, not just plain translation). Positioned at y=100 (pre-transform) so its
        // translated screen region (y:[150,170)) doesn't overlap the first sprite's (100,50)
        // check point.
        sb_->Draw(*hTex_, Rectangle(0, 100, 40, 20), Rectangle(0, 0, 2, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {   0,   0, kBlack, "(0,0):     origin - should be Black (sprite was translated away)" },
            { 100,  50, kRed,   "(100,50):  translated position - should be Red"                  },
            { 110, 160, kBlue,  "(110,160): FlipH+transform left half - should be Blue"           },
            { 130, 160, kRed,   "(130,160): FlipH+transform right half - should be Red"           },
        };

        for (const auto& c : checks)
        {
            Color got(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            const bool ok = colourMatch(got, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    SdlTransformMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(200);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTransformMatrixTest game;
    game.Run();
    return game.getResult();
}
