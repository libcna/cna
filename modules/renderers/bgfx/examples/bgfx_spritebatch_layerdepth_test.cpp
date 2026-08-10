// SPDX-License-Identifier: MS-PL
// Task 803: verify SpriteSortMode ordering affects on-screen draw order on Bgfx.
//
// Bgfx-specific adaptation of examples/easygl_spritebatch_layerdepth_test.cpp (Task 420). Not a
// verbatim reuse: that file reads back 3 spatially-separate regions from a SINGLE rendered frame
// (one SpriteBatch Begin/Draw/End, then 3 GetBackBufferData calls), but Bgfx's own
// GetBackBufferData only reliably reflects the FIRST read per rendered frame (Task 406 finding) --
// restructured into one separately-read RunCheck() pass per region, mirroring Tasks 759-768's
// established Bgfx pattern (each check redoes the full Clear+Begin/Draw/End sequence).
//
// Scope note (matches Task 420's own established scope on EasyGL): Tasks 415/416 already proved,
// via a mock/recording renderer, that SpriteSortMode::FrontToBack/BackToFront/Deferred/Texture
// deliver draw calls to the SpriteBatch renderer in the CORRECT order for each mode -- that CPU-side
// sorting logic is renderer-agnostic and already fully covered there for all 4 modes. This task (like
// Task 420 before it) verifies the remaining, genuinely renderer-specific question: that whatever
// order the renderer receives draws in is ACTUALLY reflected on screen via simple painter's algorithm
// (CNA's sprite vertices carry no Z component -- confirmed by reading BgfxSpriteBatchRenderer::Draw
// directly: layerDepth is used only as a CPU-side sort key, never written into vertex data), with no
// renderer-specific surprise (e.g. accidental depth-test interference). SpriteSortMode::FrontToBack
// is used as the representative mode, matching Task 420's own precedent.
//
// Design: 2 overlapping 60x60 opaque sprites.
//   Sprite A (Red):  destRect=(100,100,60,60), layerDepth=0.1 (smaller depth)
//   Sprite B (Blue): destRect=(130,100,60,60), layerDepth=0.9 (larger depth)
// Overlap region: x:[130,160), y:[100,160).
//
// Under SpriteSortMode::FrontToBack (ascending depth), the correct draw order is A (0.1) then B
// (0.9) -- B ends up on top in the overlap. Deliberately submitted in the OPPOSITE order in code
// (Draw(B) first, Draw(A) second) so this test genuinely discriminates depth-based sorting from
// raw submission order.
//
//   (115,130) -> Red  (A-only region)
//   (145,130) -> Blue (overlap; correct FrontToBack order draws B last -> B on top)
//   (175,130) -> Blue (B-only region)
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

    const Color kRed  (255,   0,   0, 255);
    const Color kBlue (  0,   0, 255, 255);
}

class BgfxSpriteBatchLayerDepthTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;
    std::unique_ptr<Texture2D>             blueTex_;

    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, int x, int y)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);

            sb_->Begin(SpriteSortMode::FrontToBack,
                       BlendState::Opaque,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

            // Deliberately submitted in the OPPOSITE order from the correct FrontToBack draw
            // order, so this test discriminates depth-based sorting from raw submission order.
            sb_->Draw(*blueTex_, Rectangle(130, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                      0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
            sb_->Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                      0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);

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

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 115, 130, kRed,  "A-only region -> Red" },
            { 145, 130, kBlue, "Overlap region -> Blue (B drawn last under correct FrontToBack order)" },
            { 175, 130, kBlue, "B-only region -> Blue" },
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
    BgfxSpriteBatchLayerDepthTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(300);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxSpriteBatchLayerDepthTest game;
    game.Run();
    return game.getResult();
}
