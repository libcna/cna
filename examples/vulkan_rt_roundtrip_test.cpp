// SPDX-License-Identifier: MS-PL
// Task 875: verify a real, XNA-legal Clear-only render-target pattern (no draw call between
// SetRenderTarget and SetRenderTarget(nullptr)) actually takes effect on Vulkan.
//
// Before this task, VulkanGraphicsBackend::Clear()/ClearColorAndDepth() only recorded a global
// clear-colour scalar and never added the currently-bound render target to
// RecordCommandBuffer's `usedRTs` list -- only an actual draw call did. Consequence:
// `SetRenderTarget(rt); Clear(color); SetRenderTarget(nullptr);` with no draw call in between
// silently never got a render pass recorded at all -- the target's colour image stayed at
// VK_IMAGE_LAYOUT_UNDEFINED forever (confirmed via a live Vulkan validation error the moment
// anything later tried to sample it).
//
// Method: fill a RenderTarget2D via Clear()-only (no draw call), unbind, then sample it back onto
// the backbuffer via SpriteBatch (the same proven-good RenderTarget2D-as-texture path used by
// vulkan_rt2d_test.cpp, Task 148) and read the backbuffer centre pixel.
//
// This deliberately does NOT read the RT directly while still bound (the EasyGL original this
// ports from, easygl_rt_roundtrip_test.cpp, does exactly that) -- confirmed via investigation
// that GetBackBufferData/ReadBackbuffer on Vulkan always reads the swapchain image, never
// whatever render target happens to be currently bound (unlike EasyGL, which tracks
// currentRtHeight_ and redirects). Reading "while bound" on Vulkan would not read the RT's own
// image at all. (Before REMED-GFX-140/143 it could also appear to work coincidentally, because the
// swapchain pass and every RT pass shared ONE frame-global clear-colour scalar; clear values now
// belong to the bind cycle that issued them, on the backbuffer as well as on a target.) Sampling via SpriteBatch after unbinding is the only
// methodology that genuinely exercises "does this RT's colour image contain what Clear() wrote,
// and is it in a layout a later draw can actually sample".
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 8;

class VulkanRtRoundtripTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    int pass_   = 0;
    int fail_   = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    // Clear()-only fill (NO draw call) of a fresh RenderTarget2D, unbind, then sample it back
    // onto the backbuffer via SpriteBatch and return the centre pixel.
    Color ClearOnlyThenSample(GraphicsDevice& dev, const Color& fillColor)
    {
        RenderTarget2D rt(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        dev.SetRenderTarget(&rt);
        dev.Clear(fillColor);   // the exact bug scenario: no draw call follows
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // REMED-GFX-143 false-positive audit: an artificial GetBackBufferData used to sit here,
        // forcing a frame boundary between the producer and the consumer because Vulkan had ONE
        // frame-global clear-colour scalar and the backbuffer's own Clear() further down would
        // overwrite fillColor before this target's pass was recorded. REMED-GFX-140 gave every
        // render-target bind cycle its own clear values, which made the barrier unnecessary --
        // A/B-measured: the barrier-free form already passes with GFX-140 alone. It is removed
        // here so producer and consumer sit in ONE frame with nothing between them, which is what
        // the test claimed to measure all along.

        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        dev.Clear(Color(0, 0, 0, 255));  // neutral backbuffer background before sampling
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(rt, Rectangle(0, 0, W, H), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();

        const Rectangle reg(W / 2, H / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        sb_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        Color gotGreen = ClearOnlyThenSample(dev, Color::Green);
        check(matches(gotGreen, Color::Green),
              "RT1 (green, Clear-only, no draw) sampled correctly after unbind",
              gotGreen, "(0,128,0)");

        Color gotBlue = ClearOnlyThenSample(dev, Color::Blue);
        check(matches(gotBlue, Color::Blue),
              "RT2 (blue, Clear-only, no draw) sampled correctly after unbind",
              gotBlue, "(0,0,255)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanRtRoundtripTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRtRoundtripTest game;
    game.Run();
    return game.getResult();
}
