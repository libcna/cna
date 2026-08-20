// SPDX-License-Identifier: MS-PL
// plans/plan_freedirect.md Phase X3 (DX3-20..DX3-28): texture and render-target renderer tests for the DIRECTX3
// (DirectDraw, via the ../free-direct sibling) graphics renderer.
//
// Check A -- Texture2D construction (CreateTexture, a real offscreen surface) + SetData(level=0,
//   ...) round-trips through FreeDirectTextureRenderer::UpdatePixels (Lock()/memcpy/Unlock()) without
//   throwing (DX3-20/21).
// Check B -- Texture2D::SetData(level=1, ...) throws: no native mip chain on IDirectDrawSurface
//   (DX3-22).
// Check C -- RenderTarget2D construction (CreateRenderTarget2D) succeeds.
// Check D -- SetRenderTarget(rt) + Clear(colorA) + GetBackBufferData() reads back colorA exactly
//   from the render target's OWN surface, not the shadow backbuffer (DX3-23/25/26).
// Check E -- SetRenderTarget(nullptr) + GetBackBufferData() over the same region now reads back
//   the shadow backbuffer's own (different) clear color -- proves unbinding really redirects
//   Clear()/ReadBackbuffer() back to Impl::backBuffer (DX3-26).
// Check F -- RenderTargetUsage::DiscardContents auto-clears to black on every (re)bind -- comes
//   for free from shared GraphicsDevice.cpp once bind/Clear/read are wired correctly (DX3-25).
// Check G -- SetRenderTargets() with 2 bindings throws: no MRT on IDirectDrawSurface (DX3-27).
// Check H -- Texture2D(5000, 5000) throws honestly instead of silently truncating: free-direct's
//   CreateSurface enforces a fixed 4096x4096 dimension cap (DX3-28).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 64;
static constexpr int kTargetSize = 8;

class FreeDirectTextureRenderTargetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 8;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static bool ReadsAs(GraphicsDevice& dev, const Rectangle& region, const Color& expected)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(region.Width) * region.Height, Color(0, 0, 0, 0));
        dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
        for (const Color& p : pixels)
        {
            if (p.getRProperty() != expected.getRProperty() ||
                p.getGProperty() != expected.getGProperty() ||
                p.getBProperty() != expected.getBProperty())
                return false;
        }
        return true;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Rectangle fullRegion(0, 0, kTargetSize, kTargetSize);

        // Check A/B: Texture2D construction + SetData round trip.
        {
            bool threw = false;
            try
            {
                Texture2D tex(dev, kTargetSize, kTargetSize);
                std::vector<Color> data(static_cast<std::size_t>(kTargetSize) * kTargetSize, Color(200, 100, 50, 255));
                tex.SetData(data.data(), static_cast<int>(data.size()));
            }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("Texture2D construction/SetData threw: %s\n", e.what());
            }
            check(!threw, "Texture2D construction + SetData(level=0) round-trips without throwing");
        }

        // Check C: mip level > 0 throws (no native mip chain on IDirectDrawSurface).
        {
            bool threw = false;
            try
            {
                Texture2D tex(dev, kTargetSize, kTargetSize, /*mipMap=*/true, SurfaceFormat::Color);
                std::vector<Color> data(static_cast<std::size_t>(kTargetSize / 2) * (kTargetSize / 2), Color(1, 2, 3, 255));
                tex.SetData(1, nullptr, data.data(), 0, static_cast<int>(data.size()));
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "Texture2D::SetData(level=1, ...) throws (no native mip chain)");
        }

        // Establish a known, distinct shadow-backbuffer color before any render target work.
        const Color backbufferColor(20, 40, 60, 255);
        dev.Clear(backbufferColor);

        // Check D: RenderTarget2D construction.
        std::unique_ptr<RenderTarget2D> rt;
        {
            bool threw = false;
            try { rt = std::make_unique<RenderTarget2D>(dev, kTargetSize, kTargetSize); }
            catch (const std::exception& e)
            {
                threw = true;
                std::printf("RenderTarget2D construction threw: %s\n", e.what());
            }
            check(!threw && rt != nullptr, "RenderTarget2D construction succeeds");
        }

        if (rt)
        {
            // Check E: bind + Clear + readback targets the render target's OWN surface.
            const Color rtColor(210, 30, 90, 255);
            dev.SetRenderTarget(rt.get());
            dev.Clear(rtColor);
            check(ReadsAs(dev, fullRegion, rtColor),
                  "GetBackBufferData() while bound reads back the render target's own Clear() color");

            // Check F: unbind restores the shadow backbuffer.
            dev.SetRenderTarget(nullptr);
            check(ReadsAs(dev, fullRegion, backbufferColor),
                  "GetBackBufferData() after SetRenderTarget(nullptr) restores the shadow backbuffer");

            // Check G: DiscardContents auto-clears to black on every (re)bind.
            dev.SetRenderTarget(rt.get());
            const bool discardedToBlack = ReadsAs(dev, fullRegion, Color(0, 0, 0, 255));
            dev.SetRenderTarget(nullptr);
            check(discardedToBlack,
                  "RenderTargetUsage::DiscardContents auto-clears to black on rebind (default usage)");
        }

        // Check H: SetRenderTargets() with 2 bindings throws (no MRT on IDirectDrawSurface).
        {
            bool threw = false;
            try
            {
                RenderTarget2D rt2(dev, kTargetSize, kTargetSize);
                std::vector<RenderTargetBinding> bindings;
                bindings.emplace_back(static_cast<Texture*>(rt.get()));
                bindings.emplace_back(static_cast<Texture*>(&rt2));
                dev.SetRenderTargets(bindings);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "SetRenderTargets() with 2 bindings throws (no MRT on IDirectDrawSurface)");
        }

        // Check H (DX3-28): free-direct's CreateSurface enforces a fixed 4096x4096 cap
        // (DDERR_INVALIDPARAMS outside 1..4096 per dimension); confirm CNA doesn't silently
        // truncate an oversized request before it reaches the renderer -- it must surface as a
        // real, honest throw instead.
        {
            bool threw = false;
            try { Texture2D oversized(dev, 5000, 5000); }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "Texture2D(5000, 5000) throws (free-direct's 4096x4096 CreateSurface cap, not silently truncated)");
        }

        dev.SetRenderTarget(nullptr);

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    FreeDirectTextureRenderTargetTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    FreeDirectTextureRenderTargetTest game;
    game.Run();
    return game.getResult();
}
