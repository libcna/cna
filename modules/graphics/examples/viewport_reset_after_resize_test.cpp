// SPDX-License-Identifier: MS-PL
// Task 349: verify Viewport reset behavior around a backbuffer resize.
//
// FNA's GraphicsDevice.Reset() (called by GraphicsDeviceManager.ApplyChanges() whenever the
// PresentationParameters actually change) unconditionally does:
//     Viewport = new Viewport(0, 0, PresentationParameters.BackBufferWidth, BackBufferHeight);
// regardless of whatever Viewport was set to beforehand — including a game-set custom
// sub-region Viewport (e.g. split-screen). FNA's Present() itself, by contrast, never touches
// Viewport at all.
//
// CNA has no FNA-style Reset() wired into GraphicsDeviceManager::ApplyChanges() yet (tracked
// separately); instead GraphicsDevice::UpdateViewportFromWindow() re-derives the Viewport from
// the renderer's current logical size, called both from ApplyChanges()'s renderer-sync path and
// unconditionally every GraphicsDevice::Present() call (a stronger, event-independent guarantee
// than FNA's — see Task 348, and Game::EndDraw() which calls Present() after every Draw()).
// Before this task, UpdateViewportFromWindow() decided "did the size change?" by comparing the
// renderer's computed size against viewport_'s OWN current width/height — which meant a custom
// Viewport (deliberately sized differently than the backbuffer) got silently stomped back to
// full-window size on the very next frame's Present(), even with no resize at all. Fixed by
// tracking the last-seen size in dedicated lastKnownViewportWidth_/Height_ fields instead of
// reusing viewport_'s own dimensions as the "did anything change" signal.
//
// A second, independent finding surfaced while writing this test: GraphicsDeviceManager::
// ApplyChanges() calls SDL_SetWindowSize() and then immediately queries the window size (via
// GraphicsDevice::SetVirtualResolution()'s call to UpdateViewportFromWindow()) in the same call
// stack, with no SDL event pump in between — on X11/Xvfb this resize is asynchronous, so that
// immediate query can observe a STALE physical window size and compute a transiently-wrong
// viewport width. This self-heals within a few frames, since Present() re-derives the Viewport
// every frame (see above) and will pick up the correct size once the async resize propagates —
// so this test polls across frames (mirroring Task 348's examples/easygl_real_window_resize_test.cpp
// precedent) rather than asserting immediately after ApplyChanges() returns.
//
// This test proves:
//   1. A custom sub-region Viewport survives a frame's Present() when no resize occurs (matches
//      FNA: Present() never touches Viewport) — the regression check for the fix above.
//   2. That same custom Viewport IS eventually reset to (0, 0, newBackBufferWidth,
//      newBackBufferHeight) once a genuine backbuffer resize happens via
//      GraphicsDeviceManager::ApplyChanges() — matching FNA's Reset() behavior.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kMaxWaitFrames = 300;
    constexpr int kNewBackBufferWidth  = 640;
    constexpr int kNewBackBufferHeight = 360;
}

class ViewportResetAfterResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_  = 0;
    int fail_  = 0;
    int frame_ = 0;

    int baseBackBufferWidth_  = 0;
    int baseBackBufferHeight_ = 0;

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

    void finish(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        checkDim(vp.getXProperty(), 0, "Viewport.X reset to 0 after backbuffer resize");
        checkDim(vp.getYProperty(), 0, "Viewport.Y reset to 0 after backbuffer resize");
        checkDim(vp.getWidthProperty(), kNewBackBufferWidth, "Viewport.Width reset to new backbuffer width after resize");
        checkDim(vp.getHeightProperty(), kNewBackBufferHeight, "Viewport.Height reset to new backbuffer height after resize");

        // Restore the default size so the process exits from a clean state.
        gdm_->setPreferredBackBufferWidthProperty(baseBackBufferWidth_);
        gdm_->setPreferredBackBufferHeightProperty(baseBackBufferHeight_);
        gdm_->ApplyChanges();

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 0)
        {
            baseBackBufferWidth_  = dev.getPresentationParametersProperty().getBackBufferWidthProperty();
            baseBackBufferHeight_ = dev.getPresentationParametersProperty().getBackBufferHeightProperty();

            // --- Step 1: set a custom sub-region Viewport (legal FNA usage, e.g. split-screen) ---
            Viewport custom = dev.getViewportProperty();
            custom.setXProperty(10);
            custom.setYProperty(20);
            custom.setWidthProperty(100);
            custom.setHeightProperty(60);
            dev.setViewportProperty(custom);

            ++frame_;
            return;
        }

        if (frame_ == 1)
        {
            // --- Step 2: one full frame (Draw + the framework's own EndDraw()->Present()) has
            // elapsed since the custom Viewport was set, with no resize -- it must be untouched.
            const auto& vp = dev.getViewportProperty();
            checkDim(vp.getXProperty(), 10, "Viewport.X survives a frame's Present() with no resize");
            checkDim(vp.getYProperty(), 20, "Viewport.Y survives a frame's Present() with no resize");
            checkDim(vp.getWidthProperty(), 100, "Viewport.Width survives a frame's Present() with no resize");
            checkDim(vp.getHeightProperty(), 60, "Viewport.Height survives a frame's Present() with no resize");

            // --- Step 3: trigger a genuine backbuffer resize via GDM while the custom Viewport
            // is still active.
            gdm_->setPreferredBackBufferWidthProperty(kNewBackBufferWidth);
            gdm_->setPreferredBackBufferHeightProperty(kNewBackBufferHeight);
            gdm_->ApplyChanges();

            ++frame_;
            return;
        }

        // --- Step 4: poll until the (possibly-async, see the header comment) resize has fully
        // propagated, then verify the FNA-matching reset to (0, 0, newW, newH).
        SDL_Window* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window, &physW, &physH);
        const bool physicalResizeApplied = (physW == kNewBackBufferWidth && physH == kNewBackBufferHeight);
        const auto& vp = dev.getViewportProperty();
        const bool viewportSettled = (vp.getWidthProperty() == kNewBackBufferWidth &&
                                       vp.getHeightProperty() == kNewBackBufferHeight);

        if (physicalResizeApplied && viewportSettled)
        {
            finish(dev);
            return;
        }

        if (frame_ >= kMaxWaitFrames)
        {
            check(false, "Timed out waiting for the backbuffer resize to propagate to Viewport");
            finish(dev);
            return;
        }

        ++frame_;
    }

public:
    ViewportResetAfterResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ViewportResetAfterResizeTest game;
    game.Run();
    return game.getResult();
}
