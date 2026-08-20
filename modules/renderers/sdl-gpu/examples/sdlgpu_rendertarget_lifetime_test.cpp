// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md: real regression proof for the render-target-destroyed-before-flush
// use-after-free documented in this renderer's own status banner -- this renderer batches all
// draws/clears and only actually renders them once, at Present() time (EnsureFrameRendered()),
// but a RenderTarget2D's destructor previously released its real SDL_GPUTexture* handles
// IMMEDIATELY (synchronously, at C++ scope-exit time). A render target destroyed as a short-lived
// local variable -- inside one Draw() call, before Present() ever runs -- could still have an
// ALREADY-QUEUED, not-yet-submitted draw command (e.g. a SpriteBatch draw sampling its contents
// elsewhere) referencing that now-released handle. Fixed by deferring the actual
// SDL_ReleaseGPUTexture() call until right after the next real command-buffer submit
// (SdlGpuRenderer::QueueTextureRelease/pendingTextureReleases_).
//
// Check A -- a RenderTarget2D created, drawn into, sampled by a SpriteBatch draw (queued but NOT
//   yet submitted), and then destroyed -- all as a local variable inside ONE Draw() call, before
//   this frame's Present() ever runs -- must not crash and must still render the sampled content
//   correctly (RenderTarget2D::GetData() on a SECOND, still-alive target that received the
//   SpriteBatch-drawn copy, proving the sampled draw genuinely completed, not just "didn't throw").
// Check B -- the same pattern repeated every frame for 120 frames (a fresh local RT created,
//   drawn into, sampled, and destroyed each frame) draws with no exception -- proves this is safe
//   as an ongoing pattern, not just a one-off.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kSrcSize = 8;
    constexpr int kDstSize = 16;
    constexpr int kTotalFrames = 120;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 10)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }
}

class SdlGpuRenderTargetLifetimeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rtDest_;  ///< survives -- read back to prove the copy landed

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    // Creates a render target as a LOCAL variable, draws into it, queues a SpriteBatch draw that
    // samples it into rtDest_ (still pending, not yet submitted), then lets it destruct at the
    // end of this function -- all before this frame's Present() ever runs EnsureFrameRendered().
    void DrawThroughShortLivedRenderTarget(GraphicsDevice& dev)
    {
        RenderTarget2D localRt(dev, kSrcSize, kSrcSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&localRt);
        dev.Clear(Color::Red);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtDest_.get());
        dev.Clear(Color::Blue);
        sb_->Begin();
        sb_->Draw(localRt, Rectangle(0, 0, kDstSize, kDstSize), Rectangle(0, 0, kSrcSize, kSrcSize), Color::White);
        sb_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // localRt destructs HERE -- its GPU texture handles must be deferred, not released
        // immediately, since the SpriteBatch draw above is still queued, unsubmitted, and holds a
        // raw handle into it.
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        rtDest_ = std::make_unique<RenderTarget2D>(dev, kDstSize, kDstSize, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            bool threw = false;
            const char* stage = "start";
            try
            {
                stage = "DrawThroughShortLivedRenderTarget";
                DrawThroughShortLivedRenderTarget(dev);
                Check(true, "short-lived render target created/drawn-into/sampled/destroyed within one Draw() call, no exception");
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, (std::string("threw during ") + stage + ": " + e.what()).c_str());
            }
            (void)threw;

            Color px(0, 0, 0, 0);
            const Rectangle rect(kDstSize / 2, kDstSize / 2, 1, 1);
            rtDest_->GetData(0, &rect, &px, 0, 1);
            Check(Matches(px, Color::Red),
                  ("GetData(): the short-lived render target's content genuinely reached rtDest_ via the "
                   "still-pending SpriteBatch draw, not corrupted by early destruction: got=("
                   + std::to_string(px.getRProperty()) + "," + std::to_string(px.getGProperty()) + ","
                   + std::to_string(px.getBProperty()) + ")").c_str());
        }
        else
        {
            DrawThroughShortLivedRenderTarget(dev);
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, "120 frames of short-lived-render-target churn render with no exception");
            std::printf("=== %d/3 PASS ===\n", passCount_);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuRenderTargetLifetimeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderTargetLifetimeTest game;
    game.Run();
    return game.getResult();
}
