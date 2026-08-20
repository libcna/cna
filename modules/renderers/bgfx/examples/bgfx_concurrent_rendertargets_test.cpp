// SPDX-License-Identifier: MS-PL
// Task 910: verify that binding + filling more than one RenderTarget2D within a single
// un-advanced bgfx frame no longer corrupts all but the last one bound.
//
// Root cause (see plans/plan_graphics.md/NEXT.md Task 910): bgfx::setViewFrameBuffer(viewId, fbo) is a
// per-view-per-*frame* setting, resolved once at bgfx::frame() -- not per bgfx::submit()/touch()
// call. Every render target previously shared ONE hardcoded view id (1), so binding RT_B after
// RT_A within the same frame silently redirected RT_A's already-queued clear into RT_B's
// framebuffer once the frame actually flushed, leaving RT_A's real content untouched.
//
// Methodology: prime RT_A to a known baseline colour (green) using an isolated frame (a
// safe, unambiguous starting point). Then, within a SINGLE frame and with NO bgfx::frame()
// boundary in between, bind+clear RT_A to red, then bind+clear RT_B to blue, then unbind. If the
// bug is present, RT_A's real content never actually changes (still reads back green -- the
// clear-to-red was redirected into RT_B's framebuffer instead of RT_A's); if fixed, RT_A reads
// back red and RT_B reads back blue. Each readback happens in its own later frame (one
// GetBackBufferData call per frame), avoiding the separate, already-documented "GetBackBufferData
// only reliably reflects the first read call per rendered frame" Bgfx quirk (NEXT.md §5) --
// unrelated to what this test verifies.
//
// Exit code 0 = all 3 checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 16;

class BgfxConcurrentRenderTargetsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rtA_;
    std::unique_ptr<RenderTarget2D> rtB_;
    int  step_   = 0;
    bool done_   = false;
    int  result_ = 1;
    bool primeOk_ = false, aOk_ = false, bOk_ = false;

    Color ReadOnePixel(RenderTarget2D& rt)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(rt, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
        Color px(0, 0, 0, 0);
        const Rectangle reg(kRTSize / 2, kRTSize / 2, 1, 1);
        device.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_  = std::make_unique<SpriteBatch>(device);
        rtA_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize);
        rtB_ = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        auto& device = getGraphicsDeviceProperty();

        if (step_ == 0)
        {
            // Isolated frame: prime RT_A to a known green baseline, unrelated to the concurrent
            // bind/fill under test below.
            device.SetRenderTarget(rtA_.get());
            device.Clear(Color(0, 255, 0, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ++step_;
            return;
        }
        if (step_ == 1)
        {
            const Color primed = ReadOnePixel(*rtA_);
            primeOk_ = (primed.getGProperty() >= 200 && primed.getRProperty() <= 40);
            std::printf("[%s] RT_A primed to a known green baseline: (%d,%d,%d)\n",
                        primeOk_ ? "PASS" : "FAIL",
                        primed.getRProperty(), primed.getGProperty(), primed.getBProperty());

            // The real test: bind+clear RT_A to red, then bind+clear RT_B to blue, with NO
            // bgfx::frame() boundary in between -- exactly the scenario Task 910 fixes.
            device.SetRenderTarget(rtA_.get());
            device.Clear(Color(255, 0, 0, 255));
            device.SetRenderTarget(rtB_.get());
            device.Clear(Color(0, 0, 255, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ++step_;
            return;
        }
        if (step_ == 2)
        {
            const Color a = ReadOnePixel(*rtA_);
            aOk_ = (a.getRProperty() >= 200 && a.getGProperty() <= 40 && a.getBProperty() <= 40);
            std::printf("[%s] RT_A reads back red after concurrent bind+fill: (%d,%d,%d)\n",
                        aOk_ ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
            ++step_;
            return;
        }

        const Color b = ReadOnePixel(*rtB_);
        bOk_ = (b.getBProperty() >= 200 && b.getRProperty() <= 40 && b.getGProperty() <= 40);
        std::printf("[%s] RT_B reads back blue after concurrent bind+fill: (%d,%d,%d)\n",
                    bOk_ ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        if (!aOk_ || !bOk_)
        {
            std::printf("[INFO] If RT_A/RT_B failed to show their own colour, binding a 2nd render "
                        "target within one un-advanced bgfx frame is still clobbering the 1st's "
                        "already-queued draws (Task 910's shared-view-id bug).\n");
        }

        done_ = true;
        result_ = (primeOk_ && aOk_ && bOk_) ? 0 : 1;
        Exit();
    }

public:
    BgfxConcurrentRenderTargetsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxConcurrentRenderTargetsTest game;
    game.Run();
    return game.getResult();
}
