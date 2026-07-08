// SPDX-License-Identifier: MS-PL
// Task 668: SDL_Renderer SpriteSortMode::Texture grouping pixel test.
//
// FNA/XNA's contract for SpriteSortMode::Texture is: sprites are grouped by texture (to minimize
// GPU texture-bind state changes), sorted by raw texture reference (a *pointer* comparison --
// SpriteBatch::flushBatch() does std::stable_sort(..., [](a,b){ return a.texture < b.texture; })).
// Which texture group ends up "first" on screen depends on runtime pointer addresses, not
// something a test can predict in advance -- already covered at the logic level by
// SpriteBatchTests.cpp's mock-backend `TextureGroupsDrawsByTextureAndPreservesGroupOrder` (Task
// 414), which asserts the 2 properties that ARE part of the contract (adjacency + submission-order
// stability within a group) without depending on pointer order.
//
// This test instead pixel-verifies the property a mock recording backend CAN'T: that after
// SpriteBatch reorders the draw sequence by texture, SDL_Renderer's actual draw dispatch still
// binds the CORRECT texture for each reordered call -- i.e. the sort doesn't desynchronize which
// texture ends up associated with which destination rectangle. 4 non-overlapping sprites (so the
// final result doesn't depend on which group's pointer sorts first), 2 distinct textures,
// deliberately submitted in a scrambled A/B/A/B order:
//
//   Sprite 1 (Red):  dest=(100,100,60,60)
//   Sprite 2 (Blue): dest=(200,100,60,60)
//   Sprite 3 (Red):  dest=(100,200,60,60)
//   Sprite 4 (Blue): dest=(200,200,60,60)
//
// Regardless of which texture SpriteBatch's pointer-based sort places first, each destination
// rectangle must show ITS OWN assigned colour -- a texture/position mismatch here would mean the
// backend misattributed a texture binding to the wrong reordered draw call.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
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

class SdlSpriteBatchTextureSortTest : public Game
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

        sb_->Begin(SpriteSortMode::Texture,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        // Scrambled A/B/A/B submission order, 4 non-overlapping destinations.
        sb_->Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sb_->Draw(*blueTex_, Rectangle(200, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sb_->Draw(*redTex_,  Rectangle(100, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sb_->Draw(*blueTex_, Rectangle(200, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 130, 130, kRed,  "Sprite 1 (100,100) -> Red" },
            { 230, 130, kBlue, "Sprite 2 (200,100) -> Blue" },
            { 130, 230, kRed,  "Sprite 3 (100,200) -> Red" },
            { 230, 230, kBlue, "Sprite 4 (200,200) -> Blue" },
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
    SdlSpriteBatchTextureSortTest()
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
    SdlSpriteBatchTextureSortTest game;
    game.Run();
    return game.getResult();
}
