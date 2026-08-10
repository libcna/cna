// SPDX-License-Identifier: MS-PL
// REMED-GFX-019 cross-renderer parity: SpriteBatch drawn INTO an off-screen RenderTarget2D whose
// size differs from the backbuffer must map its destination rectangle in the RENDER TARGET's own
// pixel space. EasyGL already derives its sprite ortho from the bound render target's size
// (EasyGLRenderer::FlushBatch's GetCurrentRenderTarget2DSize branch), so it is an
// INDEPENDENT oracle for the RT-relative placement the WebGPU fix must reproduce -- the geometry
// (96x72 backbuffer, 48x32 / 64x40 render targets, Rectangle(7,5,17,11) sprite) matches
// examples/webgpu_spritebatch_rendertarget_test.cpp, so a match on both renderers proves identical
// XNA pixel semantics rather than one renderer being graded against itself.
//
// The render target is read back INDIRECTLY -- blitted 1:1 onto the top-left of the backbuffer,
// then the backbuffer is read -- matching the established EasyGL render-target read-back idiom
// (easygl_rendertarget2d_mip_test.cpp / bgfx_rendertarget_viewport_test.cpp), because EasyGL does
// not populate a direct RenderTarget2D.GetData() in this configuration.
//
// The assertions are on the sprite's red-pixel BOUNDING-BOX WIDTH and HEIGHT in render-target
// pixels (17 x 11 for the 48x32 target, and the identical size for the 64x40 target), NOT on an
// absolute origin. This is deliberately invariant to EasyGL's GL-bottom-left render-target sampling
// orientation (which vertically flips the blit -- a separate, pre-existing EasyGL convention, not
// the REMED-GFX-019 defect) while still discriminating the exact failure mode: the pre-fix
// backbuffer-relative mapping would compress the sprite to roughly 8x5 render-target pixels (96/48
// x-scale, 72/32 y-scale), which these width/height bounds reject. Only Red/Black (gamma-invariant
// 0/255 channel values) are used.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferW = 96;
    constexpr int kBackbufferH = 72;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    struct BBox { int minx = 999, miny = 999, maxx = -1, maxy = -1; int count = 0;
                  bool empty() const { return maxx < 0; }
                  int width() const { return empty() ? 0 : (maxx - minx + 1); }
                  int height() const { return empty() ? 0 : (maxy - miny + 1); } };
}

class EasyGlSpriteBatchRenderTargetSizeTest : public Game
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
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void DrawRedSprite(GraphicsDevice& dev, const Rectangle& destination)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), Color::Red);
        sb.End();
    }

    // Blit the whole render target 1:1 onto the top-left of the backbuffer, then scan that WxH
    // region of the backbuffer for the red-pixel bounding box.
    BBox BlitAndScan(GraphicsDevice& dev, RenderTarget2D& rt, int rtW, int rtH)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.Clear(Color::Black);
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(rt, Rectangle(0, 0, rtW, rtH), Rectangle(0, 0, rtW, rtH), Color::White);
            sb.End();
        }
        std::vector<Color> buf(static_cast<std::size_t>(rtW) * rtH, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, rtW, rtH);
        dev.GetBackBufferData(&region, buf.data(), 0, rtW * rtH);
        BBox b;
        for (int y = 0; y < rtH; ++y)
            for (int x = 0; x < rtW; ++x)
            {
                const Color& c = buf[static_cast<std::size_t>(y) * rtW + x];
                if (c.getRProperty() > 128 && c.getGProperty() < 128 && c.getBProperty() < 128)
                { b.minx = std::min(b.minx, x); b.maxx = std::max(b.maxx, x);
                  b.miny = std::min(b.miny, y); b.maxy = std::max(b.maxy, y); ++b.count; }
            }
        return b;
    }

protected:
    void LoadContent() override
    {
        whiteTex_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A -- asymmetric RenderTarget2D (48x32): the Red sprite's render-target footprint
        // must be 17x11 pixels (Rectangle(7,5,17,11)), NOT the ~8x5 the pre-fix backbuffer-relative
        // map would produce.
        {
            RenderTarget2D rt(dev, 48, 32, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            const BBox b = BlitAndScan(dev, rt, 48, 32);
            std::printf("  RT A red bbox: x[%d..%d] y[%d..%d] w=%d h=%d count=%d\n",
                        b.minx, b.maxx, b.miny, b.maxy, b.width(), b.height(), b.count);
            check(!b.empty() && CloseTo(b.width(), 17, 2),
                  "Check A1: RT(48x32) sprite width is 17 RT pixels (RT-relative, not backbuffer "
                  "half-scale ~8): got=" + std::to_string(b.width()));
            check(!b.empty() && CloseTo(b.height(), 11, 2),
                  "Check A2: RT(48x32) sprite height is 11 RT pixels (RT-relative, not ~5): got="
                  + std::to_string(b.height()));
            check(b.width() <= 48 && b.height() <= 32 && !b.empty(),
                  "Check A3: sprite footprint fits within the render target");
        }

        // Check B -- backbuffer control: Rectangle(14,10,34,22) -> a 34x22 footprint on the 96x72
        // backbuffer (identity mapping), unaffected by GFX-019.
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(14, 10, 34, 22));
            std::vector<Color> buf(static_cast<std::size_t>(kBackbufferW) * kBackbufferH, Color(0, 0, 0, 0));
            const Rectangle whole(0, 0, kBackbufferW, kBackbufferH);
            dev.GetBackBufferData(&whole, buf.data(), 0, kBackbufferW * kBackbufferH);
            BBox b;
            for (int y = 0; y < kBackbufferH; ++y)
                for (int x = 0; x < kBackbufferW; ++x)
                {
                    const Color& c = buf[static_cast<std::size_t>(y) * kBackbufferW + x];
                    if (c.getRProperty() > 128 && c.getGProperty() < 128 && c.getBProperty() < 128)
                    { b.minx = std::min(b.minx, x); b.maxx = std::max(b.maxx, x);
                      b.miny = std::min(b.miny, y); b.maxy = std::max(b.maxy, y); }
                }
            check(!b.empty() && CloseTo(b.width(), 34, 2) && CloseTo(b.height(), 22, 2),
                  "Check B1: backbuffer sprite footprint is 34x22: got=" + std::to_string(b.width())
                  + "x" + std::to_string(b.height()));
        }

        // Check C -- two different-sized render targets, SAME pixel-space rectangle: both must show
        // a 17x11 footprint (the 64x40 target must not reuse the 48x32 target's cached dimensions).
        {
            RenderTarget2D rtA(dev, 48, 32, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D rtB(dev, 64, 40, false, SurfaceFormat::Color,
                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

            dev.SetRenderTarget(&rtA);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            const BBox ba = BlitAndScan(dev, rtA, 48, 32);

            dev.SetRenderTarget(&rtB);
            dev.Clear(Color::Black);
            DrawRedSprite(dev, Rectangle(7, 5, 17, 11));
            const BBox bb = BlitAndScan(dev, rtB, 64, 40);

            std::printf("  RT A w=%d h=%d, RT B w=%d h=%d\n", ba.width(), ba.height(),
                        bb.width(), bb.height());
            check(!ba.empty() && CloseTo(ba.width(), 17, 2) && CloseTo(ba.height(), 11, 2),
                  "Check C1: RT A(48x32) footprint 17x11");
            check(!bb.empty() && CloseTo(bb.width(), 17, 2) && CloseTo(bb.height(), 11, 2),
                  "Check C2: RT B(64x40) footprint 17x11 (same pixel rect, larger target, not cached "
                  "first-target size): got=" + std::to_string(bb.width()) + "x"
                  + std::to_string(bb.height()));
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    EasyGlSpriteBatchRenderTargetSizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferW);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferH);
    }

    int getResult() const { return result_; }
};

int main()
{
    EasyGlSpriteBatchRenderTargetSizeTest game;
    game.Run();
    return game.getResult();
}
