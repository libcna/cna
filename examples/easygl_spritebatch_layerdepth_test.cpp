// SPDX-License-Identifier: MS-PL
// Task 420: EasyGL SpriteBatch layerDepth-affects-draw-order pixel test.
//
// Tasks 415/416 already proved (via a mock/recording backend) that SpriteSortMode::FrontToBack/
// BackToFront deliver draw calls to the backend in the correct layerDepth order. This task closes
// the loop with a real GPU pixel test proving that the resulting draw ORDER actually determines
// which sprite is VISIBLE on screen where 2 opaque sprites overlap -- CNA's sprite vertices carry
// no Z component (confirmed by reading EasyGLSpriteBatchBackend::Draw directly: layerDepth is
// used only as a CPU-side sort key in flushBatch(), never written into vertex data), so with no
// depth test the sprite drawn LAST in the sorted sequence wins the overlap via simple painter's
// algorithm -- matching FNA's own default (DepthStencilState.None) SpriteBatch behaviour.
//
// Design: 2 overlapping 60x60 opaque sprites.
//   Sprite A (Red):  destRect=(100,100,60,60), layerDepth=0.1 (smaller depth)
//   Sprite B (Blue): destRect=(130,100,60,60), layerDepth=0.9 (larger depth)
// Overlap region: x:[130,160), y:[100,160).
//
// Under SpriteSortMode::FrontToBack (ascending depth), the correct draw order is A (0.1) then B
// (0.9) -- B ends up on top in the overlap.
//
// Deliberately submitted in the OPPOSITE order in code (Draw(B) first, Draw(A) second) so this
// test genuinely discriminates depth-based sorting from raw submission order: if layerDepth were
// ignored and sprites drawn in submission order instead, the actual render order would be B then
// A, and A (submitted second) would incorrectly end up on top of the overlap.
//
//   (115,130) -> Red  (A-only region)
//   (145,130) -> Blue (overlap; correct FrontToBack order draws B last -> B on top)
//   (175,130) -> Blue (B-only region)
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

class SpriteBatchLayerDepthTest : public Game
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

        sb_->Begin(SpriteSortMode::FrontToBack,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        // Deliberately submitted in the OPPOSITE order from the correct FrontToBack draw order,
        // so this test discriminates depth-based sorting from raw submission order.
        sb_->Draw(*blueTex_, Rectangle(130, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        sb_->Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 115, 130, kRed,  "A-only region -> Red" },
            { 145, 130, kBlue, "Overlap region -> Blue (B drawn last under correct FrontToBack order)" },
            { 175, 130, kBlue, "B-only region -> Blue" },
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
    SpriteBatchLayerDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchLayerDepthTest game;
    game.Run();
    return game.getResult();
}
