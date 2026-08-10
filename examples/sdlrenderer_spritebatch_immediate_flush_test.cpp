// SPDX-License-Identifier: MS-PL
// Task 670: SDL_Renderer SpriteSortMode::Immediate per-draw flush pixel test.
//
// FNA/XNA's contract for SpriteSortMode::Immediate is that each sprite is submitted to the
// graphics device the instant Draw() is called, rather than being queued and flushed in a batch
// at End() (every other sort mode's behaviour). This dispatch-level contract is already fully
// covered by the mock-renderer Task 412 tests in SpriteBatchTests.cpp
// (ImmediateFlushesInsideDrawBeforeEnd / ImmediateFlushesEachDrawSeparatelyInCallOrder), which
// prove SpriteBatch.cpp calls the renderer's Draw() immediately rather than queuing.
//
// What those tests can't prove is whether SDL_Renderer's OWN renderer (SdlSpriteBatchRenderer)
// genuinely issues a real SDL render command right there (as opposed to some renderer-internal
// queue of its own) -- confirmed by reading SdlSpriteBatchRenderer::Draw() directly: it calls
// SDL_RenderTexture() synchronously with no queue at all, so this holds trivially for SDL_Renderer
// specifically. This test instead proves the END-TO-END, real-GPU-visible consequence: that a
// plain GraphicsDevice::Clear() call issued BETWEEN two SpriteBatch operations, all inside one
// still-open Immediate Begin()/End() session, is interleaved into the real draw order exactly
// where it's called -- not deferred until End() alongside the sprites.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Sequence, all within ONE Begin(Immediate)/End() session:
//   1. Draw sprite A (Red, opaque) covering region (100,100,60,60).
//   2. dev.Clear(Green) -- wipes the ENTIRE backbuffer, A included.
//   3. End().
//
// Under a correct per-draw-flush Immediate: A is already real pixels on the render target by the
// time step 2 runs, so Clear() wipes it away -- region (100,100,60,60) must read GREEN (the clear
// colour), not red.
//
// If Immediate were silently treated like Deferred (queued, only flushed at End()): step 2's
// Clear() -- a plain GraphicsDevice call, entirely outside SpriteBatch's own queue -- still
// executes the instant it's called (SdlRenderer::Clear() is equally synchronous/unqueued),
// but sprite A would not actually render until step 3 (End()), AFTER the clear -- region
// (100,100,60,60) would incorrectly read RED, since A ends up drawn last regardless of Clear()'s
// position in the code.
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

static const Color kRed   (255,   0,   0, 255);
static const Color kGreen (  0, 255,   0, 255);

class SdlSpriteBatchImmediateFlushTest : public Game
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
        sb_ = std::make_unique<SpriteBatch>(dev);

        redTex_ = std::make_unique<Texture2D>(dev, 1, 1);
        std::vector<Color> redPx(1, kRed);
        redTex_->SetData(redPx.data(), 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Immediate,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());

        sb_->Draw(*redTex_, Rectangle(100, 100, 60, 60), Rectangle(0, 0, 1, 1), Color::White,
                  0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);

        // A plain GraphicsDevice call, entirely outside SpriteBatch's own queue -- must execute
        // right here, wiping out the sprite drawn above if (and only if) that sprite was already
        // real pixels on the render target.
        dev.Clear(kGreen);

        sb_->End();

        Color px(0, 0, 0, 0);
        Rectangle reg(130, 130, 1, 1);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        const bool ok = colourMatch(px, kGreen);
        std::printf("[%s] Sprite region after interleaved Clear() -> Green (Immediate flushed before Clear ran): got=(%d,%d,%d)\n",
            ok ? "PASS" : "FAIL", px.getRProperty(), px.getGProperty(), px.getBProperty());
        if (!ok) result_ = 1;

        Exit();
    }

public:
    SdlSpriteBatchImmediateFlushTest()
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
    SdlSpriteBatchImmediateFlushTest game;
    game.Run();
    return game.getResult();
}
