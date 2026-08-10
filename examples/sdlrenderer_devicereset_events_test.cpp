// SPDX-License-Identifier: MS-PL
// Task 715: Verify DeviceResetting/DeviceReset events fire correctly on SDL_Renderer backbuffer
// resize. Closes the Viewport/PresentationParameters section (710-715).
//
// GraphicsDevice::Reset() (shared, renderer-agnostic code) raises DeviceResetting BEFORE storing
// the new PresentationParameters, and DeviceReset AFTER the whole reconfiguration (renderer
// resolution update, viewport refresh) completes. This is exercised here specifically in the
// context of a REAL SDL_Renderer backbuffer resize (Task 711's own scenario) to confirm: both
// events fire exactly once per Reset() call, in the correct order, and DeviceResetting's handler
// observes the OLD (pre-resize) PresentationParameters while DeviceReset's handler observes the
// NEW (post-resize) PresentationParameters -- not both reflecting the same (stale or already-
// updated) state.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDeviceResetEventsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    int  resettingCount_ = 0;
    int  resetCount_     = 0;
    int  resettingSawWidth_ = -1;
    int  resetSawWidth_     = -1;
    bool resettingFiredFirst_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        dev.DeviceResetting += [this](System::Object* sender, const System::EventArgs&)
        {
            auto* d = static_cast<GraphicsDevice*>(sender);
            ++resettingCount_;
            resettingSawWidth_ = d->getPresentationParametersProperty().getBackBufferWidthProperty();
            if (resetCount_ == 0) resettingFiredFirst_ = true;
        };
        dev.DeviceReset += [this](System::Object* sender, const System::EventArgs&)
        {
            auto* d = static_cast<GraphicsDevice*>(sender);
            ++resetCount_;
            resetSawWidth_ = d->getPresentationParametersProperty().getBackBufferWidthProperty();
        };
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        check(resettingCount_ == 0 && resetCount_ == 0,
              "Neither event has fired before any explicit Reset() call");

        PresentationParameters newPP = dev.getPresentationParametersProperty();
        newPP.setBackBufferWidthProperty(64);
        newPP.setBackBufferHeightProperty(32);
        dev.Reset(newPP);

        check(resettingCount_ == 1, "DeviceResetting fires exactly once for one Reset() call");
        check(resetCount_ == 1, "DeviceReset fires exactly once for one Reset() call");
        check(resettingFiredFirst_, "DeviceResetting fires before DeviceReset");
        check(resettingSawWidth_ == 32,
              "DeviceResetting's handler observes the OLD (pre-resize) BackBufferWidth");
        check(resetSawWidth_ == 64,
              "DeviceReset's handler observes the NEW (post-resize) BackBufferWidth");

        Exit();
    }

public:
    SdlDeviceResetEventsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlDeviceResetEventsTest game;
    game.Run();
    return game.getResult();
}
