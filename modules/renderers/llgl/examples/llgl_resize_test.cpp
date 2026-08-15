// SPDX-License-Identifier: MS-PL
// plan_llgl.md LLGL-29: a real window resize, driven the way a game actually does it
// (GraphicsDeviceManager.PreferredBackBufferWidth/Height + ApplyChanges()), followed by real GPU
// pixel reads at the new resolution -- not just a check that the bookkeeping numbers changed.
//
// UpdateSwapChainResolution() (LlglRenderer.cpp) only runs at the top of Present(), so
// every resize step below is followed by PresentAfterSynchronizedWindowChange() before any pixel is read --
// without it the swap chain itself never actually resizes, only ApplyChanges()'s own presentation
// parameters would appear to have changed. The preceding GameWindow transition synchronizes the
// selected platform window before ApplyChanges() returns; the extra Present() then performs the
// renderer-side swap-chain update.
//
// Check A -- the initial size: GetViewportSize() reports the configured back buffer size, and a
//   full clear reads back correctly at the centre.
// Check B -- growing the window: GetViewportSize() reports the new, larger size, and a pixel only
//   reachable in the newly grown area reads back the fresh clear colour -- proving the swap
//   chain's own backing store was really resized, not just the reported numbers.
// Check C -- shrinking the window smaller than the original: GetViewportSize() follows again, a
//   pixel at the new bottom-right corner reads back correctly, and reading at the OLD resolution's
//   bounds (now out of range) throws rather than silently returning garbage.
// Check D -- the presentation rect recomputes from the CURRENT physical resolution, not a cached
//   one: with a square virtual canvas and Letterbox mode applied AFTER the shrink, the canvas is
//   centred in the shrunk window and the side bars still show the clear colour.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Llgl;
using CNA::Internal::Renderers::CnaPresentationMode;

namespace
{
    constexpr int kInitialWidth = 640;
    constexpr int kInitialHeight = 480;
    constexpr int kGrownWidth = 900;
    constexpr int kGrownHeight = 700;
    constexpr int kShrunkWidth = 320;
    constexpr int kShrunkHeight = 240;
    constexpr int kLogicalSize = 100;
}

class LlglResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    int result_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (!ok) result_ = 1;
    }

    static bool ChannelNear(std::uint8_t got, int want, int tolerance = 20)
    {
        return std::abs(static_cast<int>(got) - want) <= tolerance;
    }

    static bool IsColor(const std::uint8_t rgba[4], int r, int g, int b)
    {
        return ChannelNear(rgba[0], r) && ChannelNear(rgba[1], g) && ChannelNear(rgba[2], b);
    }

    // GameWindow::EndScreenDeviceChange(), reached from both the first CreateDevice() and every
    // later ApplyChanges(), synchronizes the selected IPlatformWindow after requesting its size.
    // UpdateSwapChainResolution() only runs inside Present(), so one presentation is still needed
    // before the renderer observes that synchronized platform-window size. Keeping this example
    // on the GameWindow contract avoids reopening the SDL_Window escape hatch removed by PLAT-51.
    void PresentAfterSynchronizedWindowChange()
    {
        getGraphicsDeviceProperty().Present();
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> white{255, 255, 255, 255};
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<LlglRenderer&>(device.GetRenderer());

        // The first CreateDevice() already issued and synchronized its platform size request for
        // kInitialWidth/kInitialHeight before this Draw() ran. Present once so the swap chain sees
        // that final initial size, exactly as it does after each later ApplyChanges().
        PresentAfterSynchronizedWindowChange();

        // --- Check A: the initial size ------------------------------------------------------------
        {
            int width = 0, height = 0;
            renderer.GetViewportSize(width, height);
            check(width == kInitialWidth && height == kInitialHeight,
                  "initial viewport matches the configured back buffer size");

            device.Clear(Color(255, 0, 0, 255));
            std::uint8_t rgba[4] = {0, 0, 0, 0};
            renderer.ReadBackbuffer(kInitialWidth / 2, kInitialHeight / 2, 1, 1, rgba);
            check(IsColor(rgba, 255, 0, 0), "the initial clear reads back red at the centre");
        }

        // --- Check B: growing the window ----------------------------------------------------------
        {
            gdm_->setPreferredBackBufferWidthProperty(kGrownWidth);
            gdm_->setPreferredBackBufferHeightProperty(kGrownHeight);
            gdm_->ApplyChanges();
            PresentAfterSynchronizedWindowChange(); // drives UpdateSwapChainResolution()

            int width = 0, height = 0;
            renderer.GetViewportSize(width, height);
            check(width == kGrownWidth && height == kGrownHeight,
                  "GetViewportSize follows the real resize when the window grows");

            device.Clear(Color(0, 255, 0, 255));
            std::uint8_t rgba[4] = {0, 0, 0, 0};
            // Out of bounds at the OLD 640x480 resolution, inside the new, bigger one.
            renderer.ReadBackbuffer(kGrownWidth - 10, kGrownHeight - 10, 1, 1, rgba);
            check(IsColor(rgba, 0, 255, 0),
                  "a pixel only reachable in the grown area reads back correctly -- the swap "
                  "chain's own backing store was resized, not just the reported numbers");
        }

        // --- Check C: shrinking the window below the original size --------------------------------
        {
            gdm_->setPreferredBackBufferWidthProperty(kShrunkWidth);
            gdm_->setPreferredBackBufferHeightProperty(kShrunkHeight);
            gdm_->ApplyChanges();
            PresentAfterSynchronizedWindowChange();

            int width = 0, height = 0;
            renderer.GetViewportSize(width, height);
            check(width == kShrunkWidth && height == kShrunkHeight,
                  "GetViewportSize follows the real resize when the window shrinks");

            device.Clear(Color(0, 0, 255, 255));
            std::uint8_t rgba[4] = {0, 0, 0, 0};
            renderer.ReadBackbuffer(kShrunkWidth - 1, kShrunkHeight - 1, 1, 1, rgba);
            check(IsColor(rgba, 0, 0, 255),
                  "the bottom-right corner of the shrunk back buffer reads back correctly");

            bool threw = false;
            try
            {
                // The old 640x480 bottom-right corner is now well outside the shrunk 320x240
                // back buffer.
                renderer.ReadBackbuffer(kInitialWidth - 1, kInitialHeight - 1, 1, 1, rgba);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw,
                  "reading at the OLD resolution's bounds now throws -- the back buffer genuinely "
                  "shrank rather than only reporting a new size");
        }

        // --- Check D: the presentation rect follows the CURRENT resolution, not a cached one ------
        {
            // Applied AFTER the shrink above: GraphicsDeviceManager.ApplyChanges() resets the
            // renderer's virtual resolution to match the (physical) back buffer size on every call
            // (GraphicsDevice::Reset()), so a custom virtual canvas only survives if set afterward
            // -- exactly what a real game's own post-resize logic would have to do too.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
            renderer.SetVirtualResolution(kLogicalSize, kLogicalSize);

            device.Clear(Color(0, 0, 0, 255));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, kLogicalSize, kLogicalSize),
                               Rectangle(0, 0, 1, 1), Color(255, 255, 255, 255));
            spriteBatch_->End();

            // ReadBackbuffer() addresses LOGICAL (virtual-resolution) coordinates, which cannot
            // name a letterbox bar at all -- the bars are outside the 100x100 canvas by
            // definition. So, matching llgl_presentation_test.cpp's own established pattern,
            // switch to a 1:1 window-sized presentation purely for reading back: the sprite
            // geometry was already baked into window pixels when Draw() ran above, so this switch
            // does not change what was drawn, only how ReadBackbuffer's coordinates are addressed.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
            renderer.SetVirtualResolution(kShrunkWidth, kShrunkHeight);

            // 320x240 letterboxing a square 100x100 canvas fits a 240x240 square centred at
            // x = 40..280; x = 5 and x = 315 stay in the side bars.
            std::uint8_t centre[4] = {0, 0, 0, 0};
            std::uint8_t bar[4] = {0, 0, 0, 0};
            renderer.ReadBackbuffer(kShrunkWidth / 2, kShrunkHeight / 2, 1, 1, centre);
            renderer.ReadBackbuffer(5, kShrunkHeight / 2, 1, 1, bar);
            check(IsColor(centre, 255, 255, 255) && IsColor(bar, 0, 0, 0),
                  "Letterbox centres the canvas within the CURRENT (shrunk) window, not a stale "
                  "resolution, and the side bars still show the clear colour");
        }

        Exit();
    }

public:
    LlglResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kInitialWidth);
        gdm_->setPreferredBackBufferHeightProperty(kInitialHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int getResultProperty() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    LlglResizeTest game;
    game.Run();
    return game.getResultProperty();
}
