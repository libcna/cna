// SPDX-License-Identifier: MS-PL
// REMED-GFX-012: Vulkan SpriteBatch::Begin(transformMatrix) pixel integration test.
//
// Verifies that the transformMatrix passed to SpriteBatch::Begin is actually applied to sprite
// positions by the Vulkan backend. The audit found VulkanSpriteBatchBackend never overrode
// ISpriteBatchBackend::SetTransformMatrix(), so the transform fell through to the shared no-op
// default and sprites rendered as if transformMatrix were always Identity (the only one of the 14
// backends with this gap). This is the standard XNA idiom for camera-relative 2D (scrolling worlds).
//
// Design (deliberately NOT a pure translation, and with two-sided discrimination so the two ways
// the bug can manifest cannot cancel):
//   Backbuffer 400x200, black background. A 1x1 red texture drawn into destRect (10,10,20,20),
//   i.e. an untransformed 20x20 red block covering pixels x in [10,30), y in [10,30).
//
//   transform = CreateScale(2,2,1) * CreateTranslation(50,30,0)
//   XNA row-vector Vector2::Transform maps (x,y) -> (2x+50, 2y+30), so the block's corners
//   (10,10) and (30,30) map to (70,50) and (110,90): the transformed red block covers
//   pixels x in [70,110), y in [50,90) -- disjoint from the untransformed block.
//
//   Readback:
//     ( 90, 70) -> Red    PRIMARY discriminator: interior of the *transformed* block.
//                          Under the bug (Identity) this pixel is background Black.
//     ( 20, 20) -> Black  REVERSE discriminator: interior of the *untransformed* block.
//                          Under the bug the sprite is still here (Red); the fix moves it away.
//     (200,150) -> Black  CONTROL: far from both blocks, always background.
//
// The scale factor (2x) means a translation-only misimplementation would not place the block at
// (90,70) either -- so this test discriminates the full affine transform, not just translation.
//
// Exit code 0 = all 3 PASS, 1 = at least one FAIL.

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
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kBlack(  0,   0,   0, 255);
static const Color kRed  (255,   0,   0, 255);

class VulkanTransformMatrixTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             redTex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_     = std::make_unique<SpriteBatch>(dev);
        redTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> px(1, kRed);
        redTex_->SetData(px.data(), 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kBlack);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Scale by 2 then translate by (+50,+30): (x,y) -> (2x+50, 2y+30).
        const Matrix tx = Matrix::CreateScale(2.0f, 2.0f, 1.0f)
                        * Matrix::CreateTranslation(50.0f, 30.0f, 0.0f);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, tx);
        // 20x20 red block at (10,10); transform moves+scales it to (70,50)-(110,90).
        sb_->Draw(*redTex_, Rectangle(10, 10, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  90,  70, kRed,   "(90,70): interior of TRANSFORMED block -- should be Red" },
            {  20,  20, kBlack, "(20,20): interior of UNTRANSFORMED block -- should be Black (moved away)" },
            { 200, 150, kBlack, "(200,150): control far from both blocks -- should be Black" },
        };

        int passCount = 0;
        for (const auto& c : checks)
        {
            Color got(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            const bool ok = colourMatch(got, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            if (ok) ++passCount; else result_ = 1;
        }

        std::printf("=== %d/3 PASS ===\n", passCount);
        Exit();
    }

public:
    VulkanTransformMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanTransformMatrixTest game;
    game.Run();
    return game.getResult();
}
