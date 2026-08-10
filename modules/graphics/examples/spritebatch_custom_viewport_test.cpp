// SPDX-License-Identifier: MS-PL
// REMED-GFX-072: SpriteBatch clip space must be built from the active GraphicsDevice.Viewport
// (Viewport.Width/Height), NOT the full render-target/backbuffer dimensions.
//
// XNA/FNA contract (FNA SpriteBatch.cs PrepRenderState, lines ~1433-1457): the sprite projection
// is CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0, 0, 1) * transformMatrix.
// So sprite destination coordinates are VIEWPORT-LOCAL: sprite (0,0) is the viewport's top-left
// corner, and Width/Height define the projection extent. The rasterizer viewport (set separately
// to Viewport.X/Y/Width/Height) then positions the [-1,1] clip result at the viewport's screen
// rectangle. Viewport.X/Y are NEVER subtracted from the sprite coordinates -- placement is done
// entirely by the rasterizer viewport. D3D11 (live RSGetViewports) and D3D9 (live GetViewport)
// are already correct; EasyGL/Vulkan/Bgfx/SdlGpu/WebGPU/D3D12 baked the sprite NDC over the full
// target and are AFFECTED.
//
// Pre-fix failure modes (both fail the assertions below):
//   * "squish" (WebGPU/Vulkan/SdlGpu/D3D12): the rasterizer viewport is already the sub-region
//     (prior GFX-062/063/064 fixes), but the NDC was baked over the full target, so the sprite is
//     compressed into the top-left of the viewport (a 17px-wide sprite shrinks to ~7px).
//   * "ignored" (EasyGL/Bgfx): the sprite path reset the rasterizer viewport to the full target,
//     so a custom viewport did nothing -- the sprite landed at its raw full-target absolute rect.
//
// Testing strategy: assertions read the BACKBUFFER via GetBackBufferData -- the top-left-consistent
// presented image, robust across renderers and headless environments (unlike direct RenderTarget2D
// readback of a still-deferred sprite batch, which does not flush on some renderers). The whole
// 96x72 backbuffer is read ONCE per check, then probed / bounding-boxed on the CPU. The RenderTarget2D
// custom-Viewport path is the same per-renderer clip-space bake as the backbuffer path (each renderer's
// sprite NDC/ortho divides by the active Viewport regardless of RT vs backbuffer), and is additionally
// guarded against regression by the full RT test suite; the switch test covers per-batch viewports.
//
// Only the binary/full-intensity color Red is used for spatial assertions, so a possible sRGB
// encoding cannot masquerade as a coordinate error (0/255 channel values are gamma-invariant).
//
// REMED-GFX-116 added Check D: the converse of Check A. A FULL-TARGET batch followed by a
// sub-Viewport must stay full-target. Capturing the viewport only for a sub-region batch passes
// A/B/C and still fails D, which is exactly what WebGPU did between REMED-GFX-072 and this task.
//
// Scene: backbuffer 96x72, custom Viewport V=(19,11,41,29), sprite VIEWPORT-LOCAL Rectangle(5,4,17,11).
// Correct (viewport-relative) absolute footprint: x in [24,41), y in [15,26) -- width 17, height 11.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 96;
    constexpr int kBBH = 72;

    constexpr int kVpX = 19, kVpY = 11, kVpW = 41, kVpH = 29;
    constexpr int kLocX = 5, kLocY = 4, kLocW = 17, kLocH = 11;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Redish(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
    }

    struct BBox
    {
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        int count = 0;
        bool empty() const { return count == 0; }
        int width()  const { return empty() ? 0 : (maxX - minX + 1); }
        int height() const { return empty() ? 0 : (maxY - minY + 1); }
        std::string str() const
        {
            if (empty()) return "<empty>";
            return "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                 + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);
        }
    };
}

class SpriteBatchCustomViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void SetViewport(GraphicsDevice& dev, int x, int y, int w, int h)
    {
        dev.setViewportProperty(Viewport(x, y, w, h));
    }

    void DrawSprite(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    // Read the whole backbuffer and return the RED bounding box; also probe-friendly.
    std::vector<Color> ReadBackbuffer(GraphicsDevice& dev)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kBBW) * kBBH, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBBW, kBBH);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static const Color& At(const std::vector<Color>& pix, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * kBBW + x];
    }

    static BBox RedBox(const std::vector<Color>& pix)
    {
        BBox b;
        for (int y = 0; y < kBBH; ++y)
            for (int x = 0; x < kBBW; ++x)
                if (Redish(pix[static_cast<std::size_t>(y) * kBBW + x]))
                {
                    b.minX = std::min(b.minX, x); b.maxX = std::max(b.maxX, x);
                    b.minY = std::min(b.minY, y); b.maxY = std::max(b.maxY, y);
                    ++b.count;
                }
        return b;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // -------------------------------------------------------------------------------------
        // Check A -- backbuffer, custom Viewport (19,11,41,29). Sprite viewport-local (5,4,17,11).
        // Correct footprint: width 17, height 11, minX 24 (=V.X+5), minY 15 (=V.Y+4), all inside
        // the viewport rectangle. Pre-fix "squish" gives ~7x4 near (21,13); pre-fix "ignored" gives
        // the raw 17x11 at minX 5, minY 4.
        // -------------------------------------------------------------------------------------
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetViewport(dev, kVpX, kVpY, kVpW, kVpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(kLocX, kLocY, kLocW, kLocH), Color::Red);
            SetViewport(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = RedBox(pix);

            int outside = 0;
            for (int y = 0; y < kBBH; ++y)
                for (int x = 0; x < kBBW; ++x)
                    if (Redish(At(pix, x, y)) &&
                        (x < kVpX || x >= kVpX + kVpW || y < kVpY || y >= kVpY + kVpH))
                        ++outside;

            check(!b.empty(), "Check A0: custom-viewport backbuffer rendered Red pixels: " + b.str());
            check(CloseTo(b.width(), kLocW, 1),
                  "Check A1: footprint width==" + std::to_string(kLocW) + " (not squished): " + b.str());
            check(CloseTo(b.height(), kLocH, 1),
                  "Check A2: footprint height==" + std::to_string(kLocH) + " (not squished): " + b.str());
            check(CloseTo(b.minX, kVpX + kLocX, 1),
                  "Check A3: footprint minX==" + std::to_string(kVpX + kLocX)
                  + " (=V.X+localX; not full-target-absolute): " + b.str());
            check(CloseTo(b.minY, kVpY + kLocY, 1),
                  "Check A4: footprint minY==" + std::to_string(kVpY + kLocY)
                  + " (=V.Y+localY): " + b.str());
            check(outside == 0,
                  "Check A5: no red pixels outside the viewport rectangle (rasterizer clips): outside="
                  + std::to_string(outside));
        }

        // -------------------------------------------------------------------------------------
        // Check B -- backbuffer DEFAULT full Viewport control (fix must be a no-op). Sprite
        // Rectangle(14,10,34,22) => footprint 34x22 at minX 14, minY 10.
        // -------------------------------------------------------------------------------------
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetViewport(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(14, 10, 34, 22), Color::Red);
            const BBox b = RedBox(ReadBackbuffer(dev));
            check(CloseTo(b.width(), 34, 1) && CloseTo(b.height(), 22, 1),
                  "Check B1: default-viewport footprint is 34x22: " + b.str());
            check(CloseTo(b.minX, 14, 1) && CloseTo(b.minY, 10, 1),
                  "Check B2: default-viewport footprint at (14,10): " + b.str());
        }

        // -------------------------------------------------------------------------------------
        // Check C -- transformMatrix (scale 1.5 + translate (2,3)) with a custom Viewport, on the
        // backbuffer. Sprite viewport-local Rectangle(4,2,8,6): corners (4,2)..(12,8) -> transform
        // x'=1.5x+2, y'=1.5y+3 -> viewport-local rect x in [8,20], y in [6,15] (12x9) -> absolute
        // minX = V.X+8 = 27, minY = V.Y+6 = 17. Normalized by Viewport.Width/Height, NOT the target.
        // -------------------------------------------------------------------------------------
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetViewport(dev, kVpX, kVpY, kVpW, kVpH);
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                Matrix transform = Matrix::getIdentityProperty();
                transform.M11 = 1.5f;
                transform.M22 = 1.5f;
                transform.M41 = 2.0f;
                transform.M42 = 3.0f;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
                         nullptr, transform);
                sb.Draw(whiteTex_, Rectangle(4, 2, 8, 6), Rectangle(0, 0, 1, 1), Color::Red);
                sb.End();
            }
            SetViewport(dev, 0, 0, kBBW, kBBH);
            const BBox b = RedBox(ReadBackbuffer(dev));
            check(CloseTo(b.width(), 12, 1) && CloseTo(b.height(), 9, 1),
                  "Check C1: transform+custom-viewport footprint is 12x9 (transform in viewport-local "
                  "space, normalized by Viewport W/H): " + b.str());
            check(CloseTo(b.minX, kVpX + 8, 1) && CloseTo(b.minY, kVpY + 6, 1),
                  "Check C2: transform+custom-viewport footprint at (" + std::to_string(kVpX + 8)
                  + "," + std::to_string(kVpY + 6) + "): " + b.str());
        }

        // -------------------------------------------------------------------------------------
        // Check D (REMED-GFX-116) -- the mirror image of Check A: a FULL-TARGET sprite followed by
        // a sub-Viewport that no draw ever uses. Check A only proves a SUB-REGION batch is not
        // promoted to the whole target; nothing here proved the converse, and a renderer that
        // captures the viewport only for the sub-region case (WebGPU after REMED-GFX-072) then
        // resolved the full-target batch against whatever was live when it recorded the pass --
        // squeezing this sprite into the trailing sub-Viewport. The viewport is deliberately NOT
        // restored before the read: that restore is what would hide the defect.
        // Sprite Rectangle(0,0,kBBW,kBBH) => the whole backbuffer must be red.
        // -------------------------------------------------------------------------------------
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetViewport(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), Color::Red);
            SetViewport(dev, kVpX, kVpY, kVpW, kVpH);   // used by nothing; must change nothing
            const std::vector<Color> pix = ReadBackbuffer(dev);
            SetViewport(dev, 0, 0, kBBW, kBBH);
            const BBox b = RedBox(pix);
            check(CloseTo(b.width(), kBBW, 1) && CloseTo(b.height(), kBBH, 1),
                  "Check D1: a full-target batch stays full-target when a sub-Viewport is set "
                  "afterwards: " + b.str());
            check(b.minX == 0 && b.minY == 0,
                  "Check D2: the full-target batch still starts at (0,0): " + b.str());
            check(Redish(At(pix, kBBW - 1, kBBH - 1)) && Redish(At(pix, 0, kBBH - 1)) &&
                  Redish(At(pix, kBBW - 1, 0)),
                  "Check D3: all four corners of the full-target batch are covered");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SpriteBatchCustomViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchCustomViewportTest game;
    game.Run();
    return game.getResult();
}
