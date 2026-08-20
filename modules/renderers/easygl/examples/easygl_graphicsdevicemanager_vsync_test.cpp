// SPDX-License-Identifier: MS-PL
// Verifies GraphicsDeviceManager.SynchronizeWithVerticalRetrace/ApplyChanges() actually reaches
// IGraphicsRenderer::SetSwapInterval() on a real renderer -- a real, renderer-independent gap found
// while working on plans/plan_sdlgpu.md: applyToExistingRenderer() calls GraphicsDevice::Reset(pp,
// adapter), which set every other PresentationParameters field on the renderer (virtual resolution,
// MSAA) but never forwarded PresentationInterval, unlike GraphicsDevice::SetPresentationParameters()
// (a separate, rarely-called method) which already did. Fixed by adding the same
// renderer_->SetSwapInterval(toSwapInterval(...)) forward to Reset(const PresentationParameters&,
// GraphicsAdapter*) itself, since ApplyChanges() always goes through Reset(), never
// SetPresentationParameters().
//
// Verified end-to-end on EasyGL (the default renderer) via SDL_GL_GetSwapInterval() -- a real OS/GL
// query, not a CNA-internal accessor, so this proves the value genuinely reached the driver, not
// just that CNA's own PresentationParameters field was updated.
//
// Check A -- SynchronizeWithVerticalRetrace=false + ApplyChanges() -> SDL_GL_GetSwapInterval()==0.
// Check B -- SynchronizeWithVerticalRetrace=true + ApplyChanges() -> SDL_GL_GetSwapInterval()!=0
//   (1 for standard vsync, -1 for adaptive vsync -- either is "on", unlike 0).
//
// Exit code 0 = both PASS, 1 = any FAIL, 77 = SKIPPED (see REMED-BUILD-014 note in Draw() below).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

namespace
{
    // Matches examples/common/PixelTestGame.hpp's own kSkipExitCode -- this project's established
    // SKIP_RETURN_CODE 77 convention (cmake/UnitTests.cmake's Task 470 comment) applied uniformly
    // to every registered CTest, this one included.
    constexpr int kSkipExitCode = 77;
}

using namespace Microsoft::Xna::Framework;

class EasyGLGraphicsDeviceManagerVsyncTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, int got)
    {
        std::printf("[%s] %s (SDL_GL_GetSwapInterval()=%d)\n", ok ? "PASS" : "FAIL", label, got);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        gdm_->ApplyChanges();
        int intervalOff = 0;
        SDL_GL_GetSwapInterval(&intervalOff);
        Check(intervalOff == 0, "SynchronizeWithVerticalRetrace=false reaches the real GL context", intervalOff);

        gdm_->setSynchronizeWithVerticalRetraceProperty(true);
        gdm_->ApplyChanges();
        int intervalOn = 0;
        SDL_GL_GetSwapInterval(&intervalOn);

        if (intervalOn == 0)
        {
            // REMED-BUILD-014: a virtual framebuffer (Xvfb + a software GL rasterizer) has no real
            // vertical-retrace signal to synchronize to -- SDL/the GL driver can silently refuse to
            // engage vsync there for reasons entirely outside CNA's own code. Before concluding this
            // is a regression in GraphicsDeviceManager/GraphicsDevice::Reset()'s own forwarding
            // (the thing this test exists to catch), probe the SAME real GL context directly with a
            // raw, CNA-uninvolved SDL_GL_SetSwapInterval(1) call. If even that can't make the
            // interval stick, the environment itself is incapable of real vsync here -- skip
            // cleanly (SKIP_RETURN_CODE 77, this project's established headless-skip convention,
            // see cmake/UnitTests.cmake's own Task 470 comment) rather than reporting a false
            // product regression. If the raw call DOES work, CNA's own forwarding is genuinely
            // broken and this must still fail loudly.
            const int rawSetResult = SDL_GL_SetSwapInterval(1);
            int rawProbedInterval = 0;
            SDL_GL_GetSwapInterval(&rawProbedInterval);
            if (rawSetResult != 0 || rawProbedInterval == 0)
            {
                std::printf("[SKIP] real vertical retrace is not available in this GL context "
                            "(a raw SDL_GL_SetSwapInterval(1) call, bypassing CNA entirely, also "
                            "could not make SDL_GL_GetSwapInterval() report non-zero here) -- "
                            "environment limitation, not a CNA regression\n");
                result_ = kSkipExitCode;
                Exit();
                return;
            }
        }

        Check(intervalOn != 0, "SynchronizeWithVerticalRetrace=true reaches the real GL context", intervalOn);

        std::printf("=== %d/2 PASS ===\n", passCount_);
        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    EasyGLGraphicsDeviceManagerVsyncTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGLGraphicsDeviceManagerVsyncTest game;
    game.Run();
    return game.getResult();
}
