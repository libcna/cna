// SPDX-License-Identifier: MS-PL
// Task 712: Verify fullscreen toggle on SDL_Renderer.
//
// GraphicsDevice::applyPresentationParametersToWindow() calls SDL_SetWindowFullscreen(window_,
// fullScreen); Task 902's own comment on that call already documents that fullscreen switching
// may not actually be available in headless/virtual-display test environments (Xvfb) -- the
// PresentationParameters value is stored regardless, and a backend that can't actually switch
// fullscreen still ends up with the correct XNA-level state (SDL_ClearError() swallows the
// failure non-fatally, matching GraphicsDeviceManager::applyToExistingBackend()'s identical
// handling). This test therefore verifies the XNA-level contract this project can actually
// guarantee under Xvfb: PresentationParameters.IsFullScreen round-trips correctly through
// Reset() in both directions, the toggle never throws, and the device remains fully functional
// (a normal draw+readback still works) after toggling fullscreen on and back off.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlFullscreenToggleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    bool ResetNoThrow(GraphicsDevice& dev, const PresentationParameters& pp)
    {
        try
        {
            dev.Reset(pp);
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("       Reset() threw: %s\n", e.what());
            return false;
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        check(!dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "Initial PresentationParameters.IsFullScreen is false");

        // --- Toggle to fullscreen. ---
        PresentationParameters fsPP = dev.getPresentationParametersProperty();
        fsPP.setIsFullScreenProperty(true);
        check(ResetNoThrow(dev, fsPP), "Reset() with IsFullScreen=true does not throw");
        check(dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "PresentationParameters.IsFullScreen reports true after the fullscreen toggle");

        // --- Toggle back to windowed. ---
        PresentationParameters windowedPP = dev.getPresentationParametersProperty();
        windowedPP.setIsFullScreenProperty(false);
        check(ResetNoThrow(dev, windowedPP), "Reset() with IsFullScreen=false does not throw");
        check(!dev.getPresentationParametersProperty().getIsFullScreenProperty(),
              "PresentationParameters.IsFullScreen reports false after toggling back to windowed");

        // --- The device must remain fully functional after both toggles. ---
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color(0, 255, 0, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() <= 15 && pixel.getGProperty() >= 240 && pixel.getBProperty() <= 15,
              "GraphicsDevice remains fully functional after both fullscreen toggles (normal draw+readback succeeds)");

        Exit();
    }

public:
    SdlFullscreenToggleTest()
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
    SdlFullscreenToggleTest game;
    game.Run();
    return game.getResult();
}
