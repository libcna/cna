// SPDX-License-Identifier: MS-PL
// Task 713: Verify PresentInterval (vsync) mapping to SDL_SetRenderVSync on SDL_Renderer.
//
// GraphicsDevice.cpp's own toSwapInterval() maps PresentInterval to an int: Immediate->0,
// Default/One->1, Two->2. SdlRenderer::SetSwapInterval previously collapsed ANY positive
// value down to 1 via `interval > 0 ? 1 : 0` -- silently discarding PresentInterval::Two's own
// documented semantics ("wait for two vertical retrace periods before updating; frame rate is
// capped to half the refresh rate"). SDL_SetRenderVSync's own doc comment confirms passing 2
// directly means exactly that ("synchronize present with every second vertical refresh") -- a
// real bug found and fixed: the value is now passed straight through instead of collapsed, with a
// fallback to 1 if the driver rejects the requested value outright (SDL's own doc comment: "Not
// every value is supported by every driver" -- confirmed empirically via a standalone raw SDL3
// probe that THIS project's own sandbox GL driver rejects interval=2 specifically, returning
// false and leaving the previous value in place; the fallback avoids silently leaving vsync
// unset in that case).
//
// GraphicsDevice/SdlRenderer expose no public accessor for the real SDL_Renderer's actual
// current vsync setting, matching this project's convention of not leaking renderer-specific
// types through the XNA-facing API, so this test verifies what's
// actually observable at the XNA level: every PresentInterval value can be applied via
// GraphicsDevice::Reset() without throwing, round-trips correctly through
// PresentationParameters.PresentationInterval, and the device remains fully functional afterward.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
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
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlPresentIntervalTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
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

        const PresentInterval values[] = {
            PresentInterval::Immediate, PresentInterval::One,
            PresentInterval::Two, PresentInterval::Default
        };
        const char* names[] = {"Immediate", "One", "Two", "Default"};

        for (int i = 0; i < 4; ++i)
        {
            PresentationParameters pp = dev.getPresentationParametersProperty();
            pp.setPresentationIntervalProperty(values[i]);
            dev.Reset(pp);

            char label1[96];
            std::snprintf(label1, sizeof(label1), "Reset() with PresentInterval::%s does not throw (reaching here)", names[i]);
            check(true, label1);

            char label2[128];
            std::snprintf(label2, sizeof(label2), "PresentationParameters.PresentationInterval round-trips as %s", names[i]);
            check(dev.getPresentationParametersProperty().getPresentationIntervalProperty() == values[i], label2);
        }

        // The device must remain fully functional after cycling through every interval.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color(255, 128, 0, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() >= 240 && pixel.getGProperty() >= 118 && pixel.getGProperty() <= 138 && pixel.getBProperty() <= 15,
              "GraphicsDevice remains fully functional after cycling through every PresentInterval value");

        Exit();
    }

public:
    SdlPresentIntervalTest()
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
    SdlPresentIntervalTest game;
    game.Run();
    return game.getResult();
}
