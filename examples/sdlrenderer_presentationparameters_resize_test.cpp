// SPDX-License-Identifier: MS-PL
// Task 711: Verify backbuffer resize through PresentationParameters on SDL_Renderer.
//
// GraphicsDevice::Reset(presentationParameters) stores the new PresentationParameters, calls
// applyPresentationParametersToWindow() (shared code -- calls SDL_SetWindowSize() on the real
// window), then backend_->SetVirtualResolution(width, height) to reconfigure logical presentation,
// then UpdateViewportFromWindow(). This test confirms the WHOLE chain genuinely resizes the real
// rendering surface on SDL_Renderer, not just the XNA-level PresentationParameters/Viewport
// properties -- draws a full-viewport fill, resizes to a LARGER backbuffer via Reset(), and reads
// back a pixel near the NEW, larger edge to confirm the real surface actually grew.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Real-window resizes (SDL_SetWindowSize) are ASYNCHRONOUS under X11 -- SdlGraphicsBackend::
// Present() is what detects the real output size actually changing and re-applies logical
// presentation to match (SetVirtualResolution() alone, called synchronously inside Reset(),
// only updates the backend's own stored logical size, not the real window). Reading pixels
// within the SAME Draw() call as Reset() -- before a Present() has run to let the resize land --
// throws "physical/logical size mismatch", matching this project's own deliberate no-silent-
// wrong-data design (see ReadBackbuffer's own comment). This is expected, correct behavior for
// a same-frame read, not a bug: this test defers its post-resize checks to the NEXT Draw() call,
// matching how a real game would use Reset() (typically from Update(), settling before the next
// Draw()).
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlPresentationParametersResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    int  frame_  = 0;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    void checkColor(bool ok, const char* label, Color got)
    {
        std::printf("[%s] %s: got=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        ++frame_;

        if (frame_ == 1)
        {
            // --- Initial state: 32x16, confirmed via a full-viewport fill. ---
            check(dev.getPresentationParametersProperty().getBackBufferWidthProperty() == 32
                      && dev.getPresentationParametersProperty().getBackBufferHeightProperty() == 16,
                  "Initial PresentationParameters reports the configured 32x16 backbuffer");
            check(dev.getViewportProperty().getWidthProperty() == 32 && dev.getViewportProperty().getHeightProperty() == 16,
                  "Initial Viewport matches the 32x16 backbuffer");

            dev.Clear(Color(255, 0, 255, 255));
            Color beforePx(0, 0, 0, 0);
            const Rectangle beforeRegion(30, 14, 1, 1);
            dev.GetBackBufferData(&beforeRegion, &beforePx, 0, 1);
            checkColor(beforePx.getRProperty() >= 240 && beforePx.getBProperty() >= 240,
                       "Corner near the ORIGINAL 32x16 edge reads the fill colour before resize", beforePx);

            // --- Resize to 64x32 (double each dimension) via GraphicsDevice::Reset(). ---
            PresentationParameters newPP = dev.getPresentationParametersProperty();
            newPP.setBackBufferWidthProperty(64);
            newPP.setBackBufferHeightProperty(32);
            dev.Reset(newPP);

            check(dev.getPresentationParametersProperty().getBackBufferWidthProperty() == 64
                      && dev.getPresentationParametersProperty().getBackBufferHeightProperty() == 32,
                  "PresentationParameters reports the NEW 64x32 backbuffer after Reset()");
            check(dev.getViewportProperty().getWidthProperty() == 64 && dev.getViewportProperty().getHeightProperty() == 32,
                  "Viewport matches the NEW 64x32 backbuffer after Reset()");
            // Post-resize pixel checks are deferred to the NEXT Draw() call -- see the top-of-file
            // comment on why a same-frame readback isn't a realistic or valid test here.
            return;
        }

        if (frame_ == 2)
        {
            // A fresh full-viewport fill at the NEW size -- reading back near the new, LARGER edge
            // (well past the old 32x16 bounds) proves the real rendering surface actually grew, not
            // just the XNA-level properties.
            dev.Clear(Color(0, 255, 255, 255));
            Color afterPx(0, 0, 0, 0);
            const Rectangle afterRegion(60, 28, 1, 1);
            dev.GetBackBufferData(&afterRegion, &afterPx, 0, 1);
            checkColor(afterPx.getGProperty() >= 240 && afterPx.getBProperty() >= 240,
                       "Corner near the NEW, larger 64x32 edge reads the fill colour after resize (real surface grew)",
                       afterPx);
            Exit();
        }
    }

public:
    SdlPresentationParametersResizeTest()
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
    SdlPresentationParametersResizeTest game;
    game.Run();
    return game.getResult();
}
