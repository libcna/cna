// SPDX-License-Identifier: MS-PL
// Task 669: SDL_Renderer SpriteSortMode::FrontToBack/BackToFront layerDepth-affects-draw-order
// pixel test.
//
// The FrontToBack scenario is a direct port of Task 420's EasyGL test
// (examples/easygl_spritebatch_layerdepth_test.cpp) -- same methodology, same geometry, same
// expected pixel outcomes. See that file's header comment for the full rationale (SpriteBatch.cpp's
// shared, renderer-agnostic sort logic; painter's-algorithm overlap resolution since sprite
// vertices carry no Z component). The BackToFront scenario is new for this task -- no existing
// test in this project (on any renderer) exercises SpriteSortMode::BackToFront with a real pixel
// verification.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// --- FrontToBack (top row, y=100): 2 overlapping 60x60 opaque sprites. ---
//   Sprite A (Red):  destRect=(100,100,60,60), layerDepth=0.1 (smaller depth)
//   Sprite B (Blue): destRect=(130,100,60,60), layerDepth=0.9 (larger depth)
// Under FrontToBack (ascending depth), the correct draw order is A (0.1) then B (0.9) -- B ends up
// on top in the overlap x:[130,160), y:[100,160).
//   (115,130) -> Red  (A-only region)
//   (145,130) -> Blue (overlap; correct FrontToBack order draws B last -> B on top)
//   (175,130) -> Blue (B-only region)
//
// --- BackToFront (bottom row, y=200): 2 overlapping 60x60 opaque sprites. ---
//   Sprite C (Red):  destRect=(100,200,60,60), layerDepth=0.9 (larger depth -- "back")
//   Sprite D (Blue): destRect=(130,200,60,60), layerDepth=0.1 (smaller depth -- "front")
// Under BackToFront (descending depth), the correct draw order is C (0.9) then D (0.1) -- D ends
// up on top in the overlap x:[130,160), y:[200,260).
//   (115,230) -> Red  (C-only region)
//   (145,230) -> Blue (overlap; correct BackToFront order draws D last -> D on top)
//   (175,230) -> Blue (D-only region)
//
// Both scenarios are deliberately submitted in the OPPOSITE order from their own scenario's
// correct draw order, so each genuinely discriminates depth-based sorting from raw submission
// order -- if either mode's sort were broken/ignored and sprites drawn in submission order
// instead, the overlap pixel would read the wrong colour.
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

static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);

class SdlSpriteBatchLayerDepthTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;
    std::unique_ptr<Texture2D>             blueTex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        redTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> redPx(1, kRed);
        redTex_->SetData(redPx.data(), 1);

        blueTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> bluePx(1, kBlue);
        blueTex_->SetData(bluePx.data(), 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        // --- FrontToBack: A (depth 0.1) then B (depth 0.9) is the correct draw order. ---
        sb_->Begin(SpriteSortMode::FrontToBack,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*blueTex_, Rectangle(130, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        sb_->Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        sb_->End();

        // --- BackToFront: C (depth 0.9) then D (depth 0.1) is the correct draw order. ---
        sb_->Begin(SpriteSortMode::BackToFront,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*blueTex_, Rectangle(130, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        sb_->Draw(*redTex_,  Rectangle(100, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 115, 130, kRed,  "FrontToBack A-only region -> Red" },
            { 145, 130, kBlue, "FrontToBack overlap -> Blue (B drawn last)" },
            { 175, 130, kBlue, "FrontToBack B-only region -> Blue" },
            { 115, 230, kRed,  "BackToFront C-only region -> Red" },
            { 145, 230, kBlue, "BackToFront overlap -> Blue (D drawn last)" },
            { 175, 230, kBlue, "BackToFront D-only region -> Blue" },
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
    SdlSpriteBatchLayerDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(300);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlSpriteBatchLayerDepthTest game;
    game.Run();
    return game.getResult();
}
