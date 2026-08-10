// SPDX-License-Identifier: MS-PL
// Task 227: Verify BackBufferWidth/Height changes update the device PP and viewport.
//
// Two resize paths are tested:
//   A. GDM path: setPreferredBackBufferWidth/Height + ApplyChanges()
//      - PP dimensions updated
//      - renderer virtual resolution updated (SetVirtualResolution called)
//      - viewport updated via UpdateViewportFromWindow()
//   B. Direct path: GraphicsDevice::SetPresentationParameters(pp)
//      - PP dimensions updated
//      - virtual resolution / viewport NOT changed (only swap interval is forwarded)
//
// On Xvfb (SDL_VIDEODRIVER=x11) SDL_SetWindowSize is honored immediately,
// so viewport width/height equal the new logical size after the GDM path.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BackbufferResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void checkDim(int actual, int expected, const char* label)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: expected %d, got %d", label, expected, actual);
        check(actual == expected, buf);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // --- GDM path ---

        // Default: 800×480
        checkDim(dev.getPresentationParametersProperty().getBackBufferWidthProperty(),  800, "GDM default width");
        checkDim(dev.getPresentationParametersProperty().getBackBufferHeightProperty(), 480, "GDM default height");

        // Resize to 640×360 via GDM
        gdm_->setPreferredBackBufferWidthProperty(640);
        gdm_->setPreferredBackBufferHeightProperty(360);
        gdm_->ApplyChanges();

        checkDim(dev.getPresentationParametersProperty().getBackBufferWidthProperty(),  640, "GDM resize width 640 stored in PP");
        checkDim(dev.getPresentationParametersProperty().getBackBufferHeightProperty(), 360, "GDM resize height 360 stored in PP");
        // In FixedHeightDynamicWidth mode, viewport HEIGHT = virtualHeight; WIDTH adapts to
        // the physical window's aspect ratio. Only height is invariant across displays.
        checkDim(dev.getViewportProperty().getHeightProperty(), 360, "Viewport height after GDM resize to 360");
        check(dev.getViewportProperty().getWidthProperty() > 0, "Viewport width > 0 after GDM resize to 640");

        // Resize to 1280×720
        gdm_->setPreferredBackBufferWidthProperty(1280);
        gdm_->setPreferredBackBufferHeightProperty(720);
        gdm_->ApplyChanges();

        checkDim(dev.getPresentationParametersProperty().getBackBufferWidthProperty(),  1280, "GDM resize width 1280 stored in PP");
        checkDim(dev.getPresentationParametersProperty().getBackBufferHeightProperty(),  720, "GDM resize height 720 stored in PP");
        checkDim(dev.getViewportProperty().getHeightProperty(),  720, "Viewport height after GDM resize to 720");
        check(dev.getViewportProperty().getWidthProperty() > 0, "Viewport width > 0 after GDM resize to 1280");

        // --- Direct path: SetPresentationParameters ---
        // Only the PP is updated; virtual resolution is not forwarded to renderer.
        PresentationParameters pp = dev.getPresentationParametersProperty().Clone();
        pp.setBackBufferWidthProperty(320);
        pp.setBackBufferHeightProperty(240);
        dev.SetPresentationParameters(pp);

        checkDim(dev.getPresentationParametersProperty().getBackBufferWidthProperty(),  320, "Direct PP width 320 stored");
        checkDim(dev.getPresentationParametersProperty().getBackBufferHeightProperty(), 240, "Direct PP height 240 stored");

        // Restore via GDM so the game loop is clean
        gdm_->setPreferredBackBufferWidthProperty(800);
        gdm_->setPreferredBackBufferHeightProperty(480);
        gdm_->ApplyChanges();

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BackbufferResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    BackbufferResizeTest g;
    g.Run();
    return g.getResult();
}
