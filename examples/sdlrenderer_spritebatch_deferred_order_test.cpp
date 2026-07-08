// SPDX-License-Identifier: MS-PL
// Task 667: SDL_Renderer SpriteSortMode::Deferred submission-order pixel test.
//
// SpriteSortMode::Deferred (the default) does not sort by layerDepth at all -- sprites are drawn
// in exactly the order Draw() was called (confirmed by reading SpriteBatch::flushBatch()
// directly: Deferred takes the "no sort, submission order" fall-through branch). With no depth
// test (sprite vertices carry no Z component), the sprite drawn LAST wins any overlap via simple
// painter's algorithm -- so under Deferred, submission order alone determines the outcome,
// regardless of whatever layerDepth values are passed.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Two overlapping-sprite pairs, each assigned layerDepth values that would flip the outcome if
// Deferred secretly applied FrontToBack or BackToFront sorting instead of preserving submission
// order -- together the two pairs catch either mistake:
//
//   Pair 1 (top, y=100): submit A (Red, layerDepth=0.9) then B (Blue, layerDepth=0.1).
//     Correct (submission order): B drawn last -> B on top.
//     If mistakenly sorted FrontToBack (ascending depth): B(0.1) would be drawn first, A(0.9)
//     last -> A on top -- WRONG, caught by this pair.
//
//   Pair 2 (bottom, y=200): submit C (Red, layerDepth=0.1) then D (Blue, layerDepth=0.9).
//     Correct (submission order): D drawn last -> D on top.
//     If mistakenly sorted BackToFront (descending depth): D(0.9) would be drawn first, C(0.1)
//     last -> C on top -- WRONG, caught by this pair.
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

class SdlSpriteBatchDeferredOrderTest : public Game
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

        // Deferred is the default: Begin(sortMode, blendState) overload with no explicit mode
        // argument uses SpriteSortMode::Deferred.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        // Pair 1: A (high depth) submitted first, B (low depth) submitted second.
        sb_->Draw(*redTex_,  Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);
        sb_->Draw(*blueTex_, Rectangle(130, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);

        // Pair 2: C (low depth) submitted first, D (high depth) submitted second.
        sb_->Draw(*redTex_,  Rectangle(100, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.1f);
        sb_->Draw(*blueTex_, Rectangle(130, 200, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.9f);

        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 115, 130, kRed,  "Pair1 A-only region -> Red" },
            { 145, 130, kBlue, "Pair1 overlap -> Blue (B submitted last, depth ignored)" },
            { 175, 130, kBlue, "Pair1 B-only region -> Blue" },
            { 115, 230, kRed,  "Pair2 C-only region -> Red" },
            { 145, 230, kBlue, "Pair2 overlap -> Blue (D submitted last, depth ignored)" },
            { 175, 230, kBlue, "Pair2 D-only region -> Blue" },
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
    SdlSpriteBatchDeferredOrderTest()
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
    SdlSpriteBatchDeferredOrderTest game;
    game.Run();
    return game.getResult();
}
