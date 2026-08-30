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
// REMED-GFX-243: the driver query alone made this whole case defend NOTHING wherever vsync is
// unavailable. Check A passes vacuously there -- with the forwarding deleted the interval simply
// stays 0, which is exactly what A expects -- and B skips, so deleting the very fix this file was
// written to guard left it reporting PASS and SKIP. Measured, not supposed: the forward was removed
// from GraphicsDevice::Reset() and the test still said 77.
//
// So the two questions are now asked separately. Whether CNA FORWARDED the request is CNA's own
// business and checkable anywhere; whether the DRIVER honoured it is the driver's, and a headless
// GL context routinely refuses.
//
// Check P1/P2 -- ApplyChanges() with vsync off/on reaches EasyGLRenderer::SetSwapInterval, read back
//   through GetSwapIntervalEXT(). These run everywhere and are what catch the Reset() regression.
// Check A  -- SynchronizeWithVerticalRetrace=false + ApplyChanges() -> SDL_GL_GetSwapInterval()==0.
// Check B  -- SynchronizeWithVerticalRetrace=true + ApplyChanges() -> SDL_GL_GetSwapInterval()!=0
//   (1 for standard vsync, -1 for adaptive vsync -- either is "on", unlike 0). Skipped where the
//   context has no vertical retrace to synchronize to.
//
// Exit code 0 = every check that could run passed, 1 = any FAIL. No longer 77: the plumbing half
// always runs, so reporting the whole case as skipped would hide a check that did execute.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

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

    int failCount_ = 0;
    bool driverHalfSkipped_ = false;

    void Check(bool ok, const char* label, int got)
    {
        std::printf("[%s] %s (SDL_GL_GetSwapInterval()=%d)\n", ok ? "PASS" : "FAIL", label, got);
        if (ok) ++passCount_; else ++failCount_;
    }

    void CheckForwarded(bool ok, const char* label, int got)
    {
        std::printf("[%s] %s (EasyGLRenderer::GetSwapIntervalEXT()=%d)\n",
                    ok ? "PASS" : "FAIL", label, got);
        if (ok) ++passCount_; else ++failCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& renderer = getGraphicsDeviceProperty().GetRenderer();
        if (renderer.GetSwapIntervalEXT() == -1)
        {
            // -1 means the renderer does not record what it was asked for, so the forwarding half
            // cannot be checked here at all -- a different situation from the driver declining, and
            // one this file should not paper over.
            std::printf("[FAIL] the active renderer does not report its swap interval\n");
            result_ = 1;
            Exit();
            return;
        }

        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        gdm_->ApplyChanges();
        CheckForwarded(renderer.GetSwapIntervalEXT() == 0,
                       "P1: SynchronizeWithVerticalRetrace=false is forwarded to the renderer",
                       renderer.GetSwapIntervalEXT());
        int intervalOff = 0;
        SDL_GL_GetSwapInterval(&intervalOff);
        Check(intervalOff == 0, "SynchronizeWithVerticalRetrace=false reaches the real GL context", intervalOff);

        gdm_->setSynchronizeWithVerticalRetraceProperty(true);
        gdm_->ApplyChanges();
        CheckForwarded(renderer.GetSwapIntervalEXT() != 0,
                       "P2: SynchronizeWithVerticalRetrace=true is forwarded to the renderer",
                       renderer.GetSwapIntervalEXT());
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
                            "environment limitation, not a CNA regression -- the two forwarding "
                            "checks above still ran and are what guard this file's own fix\n");
                driverHalfSkipped_ = true;
            }
        }

        if (!driverHalfSkipped_)
            Check(intervalOn != 0, "SynchronizeWithVerticalRetrace=true reaches the real GL context",
                  intervalOn);

        const int expected = driverHalfSkipped_ ? 3 : 4;
        std::printf("=== %d/%d PASS%s ===\n", passCount_, expected,
                    driverHalfSkipped_ ? " (driver half skipped)" : "");
        result_ = (failCount_ == 0 && passCount_ == expected) ? 0 : 1;
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
