// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-192, last item: a REAL window resize, as opposed to the programmatic
// `GraphicsDevice::Reset()` path `PresentationLifecycleTests.cpp` already covers.
//
// The row left this open on a boundary question rather than a technical one. The reference test
// (`easygl_real_window_resize_test.cpp`) provokes the resize with the windowing library's own
// set-window-size entry point -- a native call this project's platform boundary reserves for its
// sanctioned edges, and a renderer example is not one of them. The obvious alternative,
// `CNA::Platform::IPlatformWindow::SetSize()`, is reachable only through the private
// `GraphicsDevice::GetPlatformWindowInternal()`.
//
// THE DECISION, taken here so it is written down rather than implied: the route is
// `GameWindow::BeginScreenDeviceChange` / `EndScreenDeviceChange`, which is **public XNA 4.0 API**,
// needs no windowing-library header at all, and is exactly the call
// `GraphicsDeviceManager::ApplyChanges()` itself makes to resize the window. `GameWindow::EndScreenDeviceChange` forwards to
// `IPlatformWindow::SetSize()` + `Sync()`, so the platform window really does change size and the
// renderer really does have to notice.
//
// What separates this from a programmatic resize is what is NOT called: no `GraphicsDevice::Reset()`,
// no `GraphicsDeviceManager::ApplyChanges()`, no new `PresentationParameters`. The device is never
// told. Everything below has to arrive through the drawable size the platform reports and the
// surface the renderer reconfigures from it -- which on a swap-chain renderer is the interesting
// part, because a surface configured for the old size either produces an outdated texture or a
// validation error on the next acquire.
//
// One limitation, stated rather than glossed: `EndScreenDeviceChange` raises `ClientSizeChanged`
// itself when the bounds change, so check 4 below is weaker here than in the natively driven
// reference, where the event can only come from the platform's own resize notification. It still checks the
// XNA event contract a game subscribes to; it does not, on its own, prove the platform event path.
// Proving THAT needs a native call, which is the boundary this test exists to respect.
//
// The first three checks mirror the reference test's, including its confirmed CNA divergence:
//   1. Viewport height stays pinned to PreferredBackBufferHeight -- the default
//      PresentationMode::FixedHeightDynamicWidth only lets width track the window.
//   2. Viewport width actually changed, i.e. the new window size reached the device end to end.
//   3. PresentationParameters.BackBufferWidth/Height do NOT follow. CNA diverges from FNA here on
//      purpose (see GraphicsDeviceManager::INTERNAL_OnClientSizeChanged): forwarding the physical
//      size into BackBufferWidth/Height would corrupt FixedHeightDynamicWidth's virtual resolution.
//   4. GameWindow.ClientSizeChanged fired.
// And two this renderer needs and the reference does not:
//   5. The whole resize raised NO wgpu-native validation error -- the surface was reconfigured
//      rather than acquired at the stale size.
//   6. The device still DRAWS afterwards, checked by clearing to a distinctive colour and reading
//      the backbuffer centre back. "No errors" alone is satisfied by a renderer that quietly stopped
//      submitting anything.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kMaxWaitFrames = 300;
    /// REMED-BUILD-010's reason, kept: a window that is not focused or mapped the way an isolated
    /// Xvfb server is can have its frame rate throttled, so a frame count alone can eat the CTest
    /// timeout with no diagnostic. This deadline fails fast with an actionable message instead.
    constexpr auto kWallClockDeadline = std::chrono::seconds(20);
    const Color kAfterResizeClear(17, 140, 96, 255);
}

class WebGpuRealWindowResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    int frame_ = 0;
    int clientSizeChangedCount_ = 0;

    int initialViewportWidth_ = 0;
    int initialViewportHeight_ = 0;
    int initialBackBufferWidth_ = 0;
    int initialBackBufferHeight_ = 0;
    int targetClientWidth_ = 0;
    int targetClientHeight_ = 0;
    std::size_t errorsBefore_ = 0;
    std::chrono::steady_clock::time_point deadline_{};

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    void finish(GraphicsDevice& device)
    {
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
        const int newViewportWidth = device.getViewportProperty().getWidthProperty();
        const int newViewportHeight = device.getViewportProperty().getHeightProperty();
        const auto& parameters = device.getPresentationParametersProperty();

        check(newViewportHeight == initialViewportHeight_,
              "Viewport height stays pinned to PreferredBackBufferHeight after a real window resize "
              "(FixedHeightDynamicWidth)");
        check(newViewportWidth != initialViewportWidth_,
              "Viewport width changed after a real window resize -- the new window size reached the "
              "device without anyone calling Reset()");
        check(parameters.getBackBufferWidthProperty() == initialBackBufferWidth_ &&
                  parameters.getBackBufferHeightProperty() == initialBackBufferHeight_,
              "PresentationParameters.BackBufferWidth/Height do NOT follow a real window resize "
              "(CNA's confirmed, deliberate divergence from FNA)");
        check(clientSizeChangedCount_ > 0,
              "GameWindow.ClientSizeChanged fired for the resize (see the header for why this check "
              "is weaker here than in the natively driven reference)");

        // 5 and 6: the renderer's own half.
        device.Clear(kAfterResizeClear);
        const Rectangle centre(newViewportWidth / 2, newViewportHeight / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        check(pixel.getRProperty() == kAfterResizeClear.getRProperty() &&
                  pixel.getGProperty() == kAfterResizeClear.getGProperty() &&
                  pixel.getBProperty() == kAfterResizeClear.getBProperty(),
              "the device still draws after the resize -- a clear into the RESIZED backbuffer reads "
              "back its own colour, so the surface was reconfigured rather than abandoned");
        check(renderer.GetUncapturedErrorCountEXT() == errorsBefore_,
              "the whole resize raised no wgpu-native validation error -- no acquire against a "
              "surface still configured for the old size");

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
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(device.GetRenderer());
        GameWindow& window = getWindowProperty();

        if (frame_ == 0)
        {
            initialViewportWidth_ = device.getViewportProperty().getWidthProperty();
            initialViewportHeight_ = device.getViewportProperty().getHeightProperty();
            const auto& parameters = device.getPresentationParametersProperty();
            initialBackBufferWidth_ = parameters.getBackBufferWidthProperty();
            initialBackBufferHeight_ = parameters.getBackBufferHeightProperty();
            errorsBefore_ = renderer.GetUncapturedErrorCountEXT();

            const Rectangle bounds = window.getClientBoundsProperty();
            targetClientWidth_ = bounds.Width * 2;
            targetClientHeight_ = bounds.Height + 200;

            // The real window resize, through public XNA API and nothing else. No Reset(), no
            // ApplyChanges(), no new PresentationParameters -- the device is never told.
            window.BeginScreenDeviceChange(false);
            window.EndScreenDeviceChange(std::string(), targetClientWidth_, targetClientHeight_);

            deadline_ = std::chrono::steady_clock::now() + kWallClockDeadline;
            ++frame_;
            return;
        }

        const Rectangle bounds = window.getClientBoundsProperty();
        const bool resizeApplied =
            bounds.Width == targetClientWidth_ && bounds.Height == targetClientHeight_;
        const bool viewportUpdated =
            device.getViewportProperty().getWidthProperty() != initialViewportWidth_;

        if (resizeApplied && viewportUpdated)
        {
            finish(device);
            return;
        }

        if (std::chrono::steady_clock::now() >= deadline_)
        {
            check(false, "Timed out waiting for the real window resize to propagate within the "
                         "wall-clock deadline -- a live compositor can throttle an unfocused or "
                         "unmapped window indefinitely, which is an environment sensitivity rather "
                         "than a defect in the resize path");
            finish(device);
            return;
        }
        if (frame_ >= kMaxWaitFrames)
        {
            check(false, "Timed out waiting for the real window resize to propagate (the platform "
                         "resize never arrived)");
            finish(device);
            return;
        }
        ++frame_;
    }

public:
    WebGpuRealWindowResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    [[nodiscard]] int getResultProperty() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    WebGpuRealWindowResizeTest game;
    game.Run();
    return game.getResultProperty();
}
