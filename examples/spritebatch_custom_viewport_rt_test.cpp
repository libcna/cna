// SPDX-License-Identifier: MS-PL
// REMED-GFX-072 (RenderTarget2D-readback variant): SpriteBatch clip space must be built from the
// active GraphicsDevice.Viewport (Viewport.Width/Height), NOT the full render-target dimensions.
//
// Same XNA/FNA contract and defect as spritebatch_custom_viewport_test.cpp (see that file's header),
// but every scene renders into an off-screen RenderTarget2D and reads it back with GetData(). This
// is for backends that do not implement GetBackBufferData (SdlGpu) but flush a deferred sprite batch
// into a RenderTarget2D when it is read back after unbind. Assertions use the RED/BLUE bounding box,
// invariant to a backend's RT-sample Y orientation (width/height/minX and "inside the viewport rect"
// do not depend on the Y origin; a viewport-relative Y placement stays inside the viewport either way).
//
// Scenes on a 96x72 RenderTarget2D, custom Viewport V=(19,11,41,29):
//   A -- custom Viewport, sprite viewport-local Rectangle(5,4,17,11): footprint 17x11, minX 24,
//        entirely inside the viewport rectangle.
//   B -- default full Viewport control, Rectangle(7,5,17,11): footprint 17x11 at minX 7 (no-op).
//   C -- two batches, viewports A2=(10,8,30,20)/B2=(50,40,30,25): RED viewport-local (2,2,10,8) and
//        BLUE (4,3,12,9), each placed by its own batch's viewport.
//   D -- transformMatrix (scale 1.5 + translate) with a custom Viewport.
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
    constexpr int kRtW = 96;
    constexpr int kRtH = 72;

    constexpr int kVpX = 19, kVpY = 11, kVpW = 41, kVpH = 29;
    constexpr int kLocX = 5, kLocY = 4, kLocW = 17, kLocH = 11;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool IsRed(const Color& c)  { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool IsBlue(const Color& c) { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }

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

class SpriteBatchCustomViewportRtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
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

    std::vector<Color> ReadRt(GraphicsDevice& dev)
    {
        std::vector<Color> pix(static_cast<std::size_t>(kRtW) * kRtH, Color(0, 0, 0, 0));
        rt_->GetData(0, nullptr, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    template <typename Pred>
    static BBox BoxOf(const std::vector<Color>& pix, Pred pred)
    {
        BBox b;
        for (int y = 0; y < kRtH; ++y)
            for (int x = 0; x < kRtW; ++x)
                if (pred(pix[static_cast<std::size_t>(y) * kRtW + x]))
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
        rt_ = std::make_unique<RenderTarget2D>(dev, kRtW, kRtH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // Scene A -- custom Viewport, single sprite.
        {
            dev.SetRenderTarget(rt_.get());
            SetViewport(dev, kVpX, kVpY, kVpW, kVpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(kLocX, kLocY, kLocW, kLocH), Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = ReadRt(dev);
            const BBox b = BoxOf(pix, IsRed);
            int outside = 0;
            for (int y = 0; y < kRtH; ++y)
                for (int x = 0; x < kRtW; ++x)
                    if (IsRed(pix[static_cast<std::size_t>(y) * kRtW + x]) &&
                        (x < kVpX || x >= kVpX + kVpW || y < kVpY || y >= kVpY + kVpH))
                        ++outside;
            check(CloseTo(b.width(), kLocW, 1) && CloseTo(b.height(), kLocH, 1),
                  "Check A1: custom-viewport RT footprint is " + std::to_string(kLocW) + "x"
                  + std::to_string(kLocH) + " (not squished): " + b.str());
            check(CloseTo(b.minX, kVpX + kLocX, 1),
                  "Check A2: custom-viewport footprint minX==" + std::to_string(kVpX + kLocX)
                  + " (=V.X+localX; not full-target-absolute): " + b.str());
            check(b.minY >= kVpY - 1 && b.maxY <= kVpY + kVpH + 1 && outside == 0,
                  "Check A3: footprint entirely inside the viewport rect (rasterizer clips): outside="
                  + std::to_string(outside) + " " + b.str());
        }

        // Scene B -- default full Viewport control (no-op).
        {
            dev.SetRenderTarget(rt_.get()); // resets Viewport to full RT
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(7, 5, 17, 11), Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const BBox b = BoxOf(ReadRt(dev), IsRed);
            check(CloseTo(b.width(), 17, 1) && CloseTo(b.height(), 11, 1),
                  "Check B1: default-viewport RT footprint is 17x11: " + b.str());
            check(CloseTo(b.minX, 7, 1),
                  "Check B2: default-viewport RT footprint minX==7: " + b.str());
        }

        // Scene C -- two batches, different viewports, one frame/target.
        {
            dev.SetRenderTarget(rt_.get());
            dev.Clear(Color::Black);
            SetViewport(dev, 10, 8, 30, 20);
            DrawSprite(dev, Rectangle(2, 2, 10, 8), Color::Red);
            SetViewport(dev, 50, 40, 30, 25);
            DrawSprite(dev, Rectangle(4, 3, 12, 9), Color::Blue);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> pix = ReadRt(dev);
            const BBox r = BoxOf(pix, IsRed);
            const BBox bl = BoxOf(pix, IsBlue);
            check(CloseTo(r.width(), 10, 1) && CloseTo(r.height(), 8, 1) && CloseTo(r.minX, 12, 1),
                  "Check C1: batch-1 red 10x8 at minX 12 (viewport A relative): " + r.str());
            check(CloseTo(bl.width(), 12, 1) && CloseTo(bl.height(), 9, 1) && CloseTo(bl.minX, 54, 1),
                  "Check C2: batch-2 blue 12x9 at minX 54 (viewport B relative): " + bl.str());
        }

        // Scene D -- transformMatrix (scale 1.5 + translate) with a custom Viewport.
        {
            dev.SetRenderTarget(rt_.get());
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
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const BBox b = BoxOf(ReadRt(dev), IsRed);
            check(CloseTo(b.width(), 12, 1) && CloseTo(b.height(), 9, 1) && CloseTo(b.minX, kVpX + 8, 1),
                  "Check D1: transform+custom-viewport footprint 12x9 at minX " + std::to_string(kVpX + 8)
                  + " (transform in viewport-local space, normalized by Viewport W/H): " + b.str());
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SpriteBatchCustomViewportRtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(96);
    }

    int getResult() const { return result_; }
};

int main()
{
    SpriteBatchCustomViewportRtTest game;
    game.Run();
    return game.getResult();
}
