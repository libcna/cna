// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: runtime SetSwapInterval() override (plans/plan_opengl1.md item 20, EasyGL parity).
//
// Before this, OpenGL1Renderer never overrode IGraphicsRenderer::SetSwapInterval() at all
// (inherited the interface's no-op default) -- only the construction-time swapInterval ever
// reached SDL_GL_SetSwapInterval(), so a runtime GraphicsDevice.PresentationParameters change
// (e.g. toggling vsync) silently did nothing. Verified directly against the real driver via
// SDL_GL_GetSwapInterval() -- not just "no exception was thrown".
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL1SwapIntervalTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        dev.GetRenderer().SetSwapInterval(0);
        int gotImmediate = -999;
        SDL_GL_GetSwapInterval(&gotImmediate);
        std::printf("after SetSwapInterval(0): SDL_GL_GetSwapInterval=%d\n", gotImmediate);
        Check(gotImmediate == 0, "SetSwapInterval(0) reaches SDL (immediate/no vsync)");

        dev.GetRenderer().SetSwapInterval(1);
        int gotVsync = -999;
        SDL_GL_GetSwapInterval(&gotVsync);
        std::printf("after SetSwapInterval(1): SDL_GL_GetSwapInterval=%d\n", gotVsync);
        if (gotVsync != 1 && !SDL_GL_SetSwapInterval(1))
        {
            // The DRIVER itself refuses vsync on this display: a direct SDL_GL_SetSwapInterval(1)
            // -- no CNA code involved -- fails too (GLX swap control absent, e.g. Xvfb/llvmpipe).
            // The override demonstrably forwards (the interval-0 check above reached SDL); what
            // cannot be observed here is the driver honouring 1. An honest skip, not a pass.
            std::printf("[SKIP] SetSwapInterval(1): driver refuses swap control on this display "
                        "(raw SDL_GL_SetSwapInterval(1) fails: %s)\n", SDL_GetError());
        }
        else
        {
            Check(gotVsync == 1, "SetSwapInterval(1) reaches SDL (vsync) -- proves this is a real "
                  "runtime change, not just a value that happened to already be set at construction");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1SwapIntervalTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1SwapIntervalTest game;
    game.Run();
    return game.getResult();
}
