// SPDX-License-Identifier: MS-PL
// Task 168: EasyGL SpriteBatch::Begin transformMatrix pixel integration test.
//
// Verifies that the transformMatrix parameter passed to SpriteBatch::Begin is
// correctly applied to sprite positions by the EasyGL backend.
//
// Design:
//   Viewport 400×200, black background.
//   A 1×1 red texture is drawn at position (0,0) with a translation matrix
//   Matrix::CreateTranslation(100, 50, 0).  The sprite should appear at (100,50).
//
//   Readback:
//     (0,   0)  → Black  (original position, sprite was moved away)
//     (100, 50) → Red    (translated position)
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

class TransformMatrixTest : public Game
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
        dev.Clear(kBlack);

        // Translate every sprite by (+100, +50).
        Matrix tx = Matrix::CreateTranslation(100.0f, 50.0f, 0.0f);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, tx);

        // Draw the 1×1 red sprite at (0,0) — transform moves it to (100,50).
        sb_->Draw(*redTex_, Vector2(0.0f, 0.0f), Color::White);

        sb_->End();

        // ── readback checks ─────────────────────────────────────────────────
        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {   0,  0, kBlack, "(0,0):   origin — should be Black (sprite was translated away)" },
            { 100, 50, kRed,   "(100,50): translated position — should be Red"                  },
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
    TransformMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(400);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    TransformMatrixTest game;
    game.Run();
    return game.getResult();
}
