// SPDX-License-Identifier: MS-PL
// Task 224: Verify IsFullScreen is stored consistently in the device PP.
//
// SDL_SetWindowFullscreen may fail in headless / virtual-display environments.
// That failure must not throw — the PP value is stored before the SDL call, so
// a renderer that cannot switch fullscreen still has the correct stored state.
// This test verifies the round-trip via the GDM property + ApplyChanges() path.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class FullScreenFieldTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // After GDM ctor + ApplyChanges(), default IsFullScreen=false must be stored.
        check(!dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "Default IsFullScreen=false stored in device PP");

        // Set true and apply — must not throw even if SDL cannot switch fullscreen.
        gdm_->setIsFullScreenProperty(true);
        gdm_->ApplyChanges();
        check(dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "IsFullScreen=true stored after ApplyChanges");
        check(gdm_->getIsFullScreenProperty(),
              "GDM getter returns true after setIsFullScreen(true)");

        // Set back to false.
        gdm_->setIsFullScreenProperty(false);
        gdm_->ApplyChanges();
        check(!dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "IsFullScreen=false stored after ApplyChanges");

        // ToggleFullScreen() must flip and store the new value.
        gdm_->ToggleFullScreen();
        check(dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "ToggleFullScreen() stored IsFullScreen=true in device PP");
        check(gdm_->getIsFullScreenProperty() ==
              dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "GDM getter matches device PP after ToggleFullScreen");

        // Toggle back — confirms toggle is idempotent.
        gdm_->ToggleFullScreen();
        check(!dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "ToggleFullScreen() stored IsFullScreen=false in device PP");

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    FullScreenFieldTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    FullScreenFieldTest g;
    g.Run();
    return g.getResult();
}
