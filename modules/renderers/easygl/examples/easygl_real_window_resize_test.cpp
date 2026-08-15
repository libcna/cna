// SPDX-License-Identifier: MS-PL
// Task 348: verify Viewport tracks a REAL OS-level window resize, and that
// GameWindow.ClientSizeChanged actually fires for it.
//
// Task 227's examples/easygl_backbuffer_resize_test.cpp only exercises the GDM-API-driven resize
// path (setPreferredBackBufferWidth/Height + ApplyChanges(), which calls EndScreenDeviceChange
// synchronously). Nobody had verified the OTHER path: an actual window resize coming from outside
// the game (a user dragging the window edge) — SDL_EVENT_WINDOW_RESIZED -> Game's event loop ->
// GameWindow::updateFromPlatform() -> GameWindow.ClientSizeChanged.
//
// IMPORTANT DISCOVERY made while verifying this test's discriminating power (temporarily
// disabling GraphicsDeviceManager's ClientSizeChanged subscription and re-running): CNA's Viewport
// tracks the window on EVERY frame regardless of that event, because GraphicsDevice::Present()
// unconditionally calls UpdateViewportFromWindow() every frame. So check 2 below (viewport width
// changed) does NOT actually depend on ClientSizeChanged firing — it would pass even if that
// wiring were silently broken. This is a *stronger* guarantee than FNA's own event-driven design
// (a dropped/delayed OS resize event can't leave CNA's viewport stale for more than one frame),
// but it also means ClientSizeChanged's own FNA-API contract (firing so *game* code that
// subscribes to it gets notified) needed its own, independent check — added as check 4 below,
// which SUBSCRIBES to ClientSizeChanged directly and would fail on its own if that specific event
// stopped firing, independent of Present()'s redundant per-frame refresh.
//
// This test resizes the real SDL window directly via SDL_SetWindowSize (bypassing
// GraphicsDeviceManager entirely) and polls for a few frames until the async X11/Xvfb resize
// event has propagated, then checks:
//   1. Viewport height stays pinned to PreferredBackBufferHeight (CNA's default
//      PresentationMode::FixedHeightDynamicWidth only lets width track the window).
//   2. Viewport width actually changed (proves the real window size is reflected end to end).
//   3. PresentationParameters.BackBufferWidth/Height do NOT change. This is CNA's confirmed,
//      deliberate divergence from FNA (see GraphicsDeviceManager::INTERNAL_OnClientSizeChanged's
//      own comment: forwarding the physical window size into BackBufferWidth/Height on every
//      resize, as FNA does, would corrupt FixedHeightDynamicWidth's virtual-resolution scaling).
//      Pinned here as a regression marker — see plan_graphics.md Task 348.
//   4. GameWindow.ClientSizeChanged itself fired at least once during the resize (the FNA-API
//      event contract, verified independently of check 2's Present()-driven refresh).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kMaxWaitFrames = 300;

    // REMED-BUILD-010: under a real desktop compositor a window that isn't focused/mapped the way
    // an isolated Xvfb server is can have its frame rate throttled arbitrarily (observed: this
    // test's own frame_ counter still advances, just slowly, rather than the process being
    // genuinely blocked in a syscall) -- kMaxWaitFrames alone can then take far longer in wall-clock
    // time than intended, silently eating into CTest's own 60s TIMEOUT with zero diagnostic output
    // until that blunt kill fires. This independent wall-clock deadline (checked every Draw() call,
    // not tied to frame count) fails fast with an actionable message well before that, regardless of
    // which display this runs against -- CTest's own TIMEOUT remains only a backstop for a genuine
    // full hang, not the normal way this scenario is expected to end.
    constexpr auto kWallClockDeadline = std::chrono::seconds(20);
}

class RealWindowResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_  = 0;
    int fail_  = 0;
    int frame_ = 0;
    int clientSizeChangedCount_ = 0;

    int initialViewportWidth_  = 0;
    int initialViewportHeight_ = 0;
    int initialBackBufferWidth_  = 0;
    int initialBackBufferHeight_ = 0;
    int targetPhysicalWidth_  = 0;
    int targetPhysicalHeight_ = 0;
    std::chrono::steady_clock::time_point deadline_{};

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void finish(GraphicsDevice& dev)
    {
        const int newViewportWidth  = dev.getViewportProperty().getWidthProperty();
        const int newViewportHeight = dev.getViewportProperty().getHeightProperty();
        const int newBackBufferWidth  = dev.getPresentationParametersProperty().getBackBufferWidthProperty();
        const int newBackBufferHeight = dev.getPresentationParametersProperty().getBackBufferHeightProperty();

        check(newViewportHeight == initialViewportHeight_,
              "Viewport height stays pinned to PreferredBackBufferHeight after a real window "
              "resize (FixedHeightDynamicWidth mode)");
        check(newViewportWidth != initialViewportWidth_,
              "Viewport width changed after a real window resize");
        check(newBackBufferWidth == initialBackBufferWidth_ && newBackBufferHeight == initialBackBufferHeight_,
              "PresentationParameters.BackBufferWidth/Height do NOT follow a real window resize "
              "(confirmed, deliberate divergence from FNA -- see plan_graphics.md Task 348)");
        check(clientSizeChangedCount_ > 0,
              "GameWindow.ClientSizeChanged fired at least once for the real window resize "
              "(FNA API event contract, checked independently of Present()'s per-frame refresh)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

protected:
    void Initialize() override
    {
        getWindowProperty().ClientSizeChanged +=
            [this](System::Object*, const System::EventArgs&) { ++clientSizeChangedCount_; };
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SDL_Window* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());

        if (frame_ == 0)
        {
            initialViewportWidth_  = dev.getViewportProperty().getWidthProperty();
            initialViewportHeight_ = dev.getViewportProperty().getHeightProperty();
            initialBackBufferWidth_  = dev.getPresentationParametersProperty().getBackBufferWidthProperty();
            initialBackBufferHeight_ = dev.getPresentationParametersProperty().getBackBufferHeightProperty();

            int physW = 0, physH = 0;
            SDL_GetWindowSize(window, &physW, &physH);
            targetPhysicalWidth_  = physW * 2;
            targetPhysicalHeight_ = physH + 200;

            // Resize the REAL SDL window directly -- bypasses GraphicsDeviceManager entirely,
            // simulating a user dragging the window edge rather than a game-driven API resize.
            SDL_SetWindowSize(window, targetPhysicalWidth_, targetPhysicalHeight_);

            deadline_ = std::chrono::steady_clock::now() + kWallClockDeadline;

            ++frame_;
            return;
        }

        int physW = 0, physH = 0;
        SDL_GetWindowSize(window, &physW, &physH);
        const bool physicalResizeApplied = (physW == targetPhysicalWidth_ && physH == targetPhysicalHeight_);
        const bool viewportUpdated = dev.getViewportProperty().getWidthProperty() != initialViewportWidth_;

        if (physicalResizeApplied && viewportUpdated)
        {
            finish(dev);
            return;
        }

        if (std::chrono::steady_clock::now() >= deadline_)
        {
            check(false, "Timed out waiting for the real window resize to propagate within "
                          "the wall-clock deadline (REMED-BUILD-010: a live desktop compositor can "
                          "throttle/delay this indefinitely for an unfocused or unmapped window; "
                          "this is an environment/window-manager-state sensitivity, not a defect in "
                          "the resize/viewport code itself, which check 2's own PASS already proves "
                          "correct whenever the event does arrive)");
            finish(dev);
            return;
        }

        if (frame_ >= kMaxWaitFrames)
        {
            check(false, "Timed out waiting for the real window resize to propagate "
                          "(X11/Xvfb resize event never arrived)");
            finish(dev);
            return;
        }

        ++frame_;
    }

public:
    RealWindowResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    RealWindowResizeTest game;
    game.Run();
    return game.getResult();
}
