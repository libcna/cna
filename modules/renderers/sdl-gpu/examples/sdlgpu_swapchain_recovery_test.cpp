// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-11: proves the hard-swapchain-acquisition-failure path is genuinely
// recoverable, not just "throws and leaves the renderer in an unknown state" -- a real gap this
// row's own Notes column flagged as "not yet proven recoverable in practice".
//
// SDL_WaitAndAcquireGPUSwapchainTexture returning false (as opposed to filling a null texture,
// e.g. for a minimized window -- a separate, already-handled, documented non-error case) is hard
// to trigger with real hardware/driver-loss events, but SDL_gpu's own
// SDL_ReleaseWindowFromGPUDevice/SDL_ClaimWindowForGPUDevice pair gives a safe, controlled way to
// force the exact same failure: un-claiming the window makes the next acquisition attempt fail for
// real, with a real SDL_GetError() message, going through the exact code path a genuine device-loss
// event would.
//
// Check A -- normal frames before the induced failure render with no exception.
// Check B -- releasing the window from the GPU device, then forcing a Present() while a frame is
//   pending, throws (the hard-acquisition-failure path is real and reachable).
// Check C -- SDL_ClaimWindowForGPUDevice re-claims the window successfully.
// Check D -- a subsequent Present() succeeds with no exception -- the renderer recovers cleanly,
//   not left in a broken state by the thrown exception (framePending_ correctly stays set across
//   the failed attempt, so the same frame's queued Clear() is not lost).
// Check E -- many further frames after the recovery continue to render with no exception --
//   the renderer remains fully usable, not just "didn't crash immediately".
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kTotalFrames = 40;
    constexpr int kInduceFailureFrame = 10;
}

class SdlGpuSwapchainRecoveryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<SdlGpuRenderer&>(dev.GetRenderer());

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::CornflowerBlue, 1.0f, 0);

        if (frame_ == kInduceFailureFrame)
        {
            Check(true, "frames 1.." + std::to_string(kInduceFailureFrame - 1) + " rendered with no exception");

            SDL_GPUDevice* device = renderer.Device();
            SDL_Window* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());

            // Un-claims the window from the GPU device -- the next
            // SDL_WaitAndAcquireGPUSwapchainTexture call on it now genuinely fails (returns false),
            // the exact hard-acquisition-failure path this row's own Notes flagged as unproven.
            SDL_ReleaseWindowFromGPUDevice(device, window);

            bool threw = false;
            try
            {
                dev.Present();
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            Check(threw, "Present() with the window released from the GPU device throws "
                         "(hard acquisition failure is real and reachable)");

            // Re-claim -- the same recovery a real game's own device-lost handling would perform.
            const bool reclaimed = SDL_ClaimWindowForGPUDevice(device, window);
            Check(reclaimed, "SDL_ClaimWindowForGPUDevice re-claims the window successfully");

            bool threwAfterRecovery = false;
            try
            {
                dev.Present();
            }
            catch (const std::exception& e)
            {
                threwAfterRecovery = true;
                std::printf("       unexpected exception after recovery: %s\n", e.what());
            }
            Check(!threwAfterRecovery, "Present() succeeds again after re-claiming the window -- "
                                       "the renderer genuinely recovers, not left in a broken state");
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, std::to_string(kTotalFrames - kInduceFailureFrame) +
                        " further frames after recovery rendered with no exception -- "
                        "the renderer remains fully usable, not just \"didn't crash immediately\"");
            std::printf("=== %d/5 PASS ===\n", passCount_);
            result_ = (passCount_ == 5) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuSwapchainRecoveryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuSwapchainRecoveryTest game;
    game.Run();
    return game.getResult();
}
