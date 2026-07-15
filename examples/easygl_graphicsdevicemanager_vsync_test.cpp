// SPDX-License-Identifier: MS-PL
// Verifies GraphicsDeviceManager.SynchronizeWithVerticalRetrace/ApplyChanges() actually reaches
// IGraphicsBackend::SetSwapInterval() on a real backend -- a real, backend-independent gap found
// while working on plan_sdlgpu.md: applyToExistingBackend() calls GraphicsDevice::Reset(pp,
// adapter), which set every other PresentationParameters field on the backend (virtual resolution,
// MSAA) but never forwarded PresentationInterval, unlike GraphicsDevice::SetPresentationParameters()
// (a separate, rarely-called method) which already did. Fixed by adding the same
// backend_->SetSwapInterval(toSwapInterval(...)) forward to Reset(const PresentationParameters&,
// GraphicsAdapter*) itself, since ApplyChanges() always goes through Reset(), never
// SetPresentationParameters().
//
// Verified end-to-end on EasyGL (the default backend) via SDL_GL_GetSwapInterval() -- a real OS/GL
// query, not a CNA-internal accessor, so this proves the value genuinely reached the driver, not
// just that CNA's own PresentationParameters field was updated.
//
// Check A -- SynchronizeWithVerticalRetrace=false + ApplyChanges() -> SDL_GL_GetSwapInterval()==0.
// Check B -- SynchronizeWithVerticalRetrace=true + ApplyChanges() -> SDL_GL_GetSwapInterval()!=0
//   (1 for standard vsync, -1 for adaptive vsync -- either is "on", unlike 0).
//
// Exit code 0 = both PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

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
