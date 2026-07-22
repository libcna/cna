// SPDX-License-Identifier: MS-PL
// REMED-GFX-073: the Software (CPU-raster) backend's SpriteBatch must honor a custom
// GraphicsDevice.Viewport, exactly like the GPU backends corrected by REMED-GFX-072.
//
// XNA/FNA contract (FNA SpriteBatch.cs PrepRenderState): the sprite projection is
// CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0, 0, 1) * transformMatrix, and
// the rasterizer viewport is set to Viewport.X/Y/Width/Height. Therefore SpriteBatch destination
// coordinates are VIEWPORT-LOCAL: sprite (0,0) is the viewport's top-left corner, Width/Height
// define the (1:1 pixel) projection/clip extent, and the rasterizer viewport positions the result
// at Viewport.X/Y. Viewport.X/Y are NEVER subtracted from sprite coordinates; pixels outside the
// viewport rectangle are clipped. D3D11 (live RSGetViewports) was the unmodified GFX-072 oracle;
// every corrected GPU backend renders the canonical scene to the byte-identical footprint
// x[24,40] y[15,25] n=187.
//
// Pre-fix Software signature (this test FAILS pre-fix): SoftwareSpriteBatchBackend placed sprite
// corners at raw framebuffer pixels (destinationRectangle.X/Y with no viewport offset) and
// SoftwareGraphicsBackend::SetViewport was a no-op, so a custom Viewport neither positioned nor
// clipped the sprite -- the canonical sprite landed at raw local (5,4), footprint x[5,21] y[4,14].
//
// Testing strategy: read the whole 96x72 backbuffer once per check via GetBackBufferData (which the
// Software backend serves from its real CPU framebuffer), then probe / bounding-box on the CPU. Only
// binary/full-intensity colors are used for spatial assertions so no possible encoding can masquerade
// as a coordinate error.
//
// Checks:
//   A  canonical custom Viewport(19,11,41,29), sprite local (5,4,17,11): footprint 17x11 at (24,15),
//      fully inside the viewport, no red outside the viewport rectangle (positioning + clipping).
//   B  default full Viewport(0,0,96,72), sprite (14,10,34,22): fix is a no-op -> 34x22 at (14,10).
//   C  transformMatrix (scale 1.5 + translate (2,3)) + custom Viewport: 12x9 at (27,17); the
//      transform composes in viewport-local space, the viewport origin is added AFTER it.
//   D  two Viewports in one frame (Red in A, Green in B): each batch uses its own live viewport.
//   E  a sprite overflowing all four viewport edges is clipped to exactly the viewport rectangle.
//   F  same sprite dest under two different Viewport sizes -> identical 17x11 sprite pixels
//      (Width/Height define the extent, they do NOT rescale 1:1 sprite pixels).
//   G  negative/partially-offscreen viewport-local rect (-5,3,20,10): left edge clips at Viewport.X.
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

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Redish(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60;
    }
    bool Greenish(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
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

class SoftwareSpriteBatchViewportTest : public Game
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

    void SetVp(GraphicsDevice& dev, int x, int y, int w, int h)
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

    template <typename Pred>
    static BBox Box(const std::vector<Color>& pix, Pred pred)
    {
        BBox b;
        for (int y = 0; y < kBBH; ++y)
            for (int x = 0; x < kBBW; ++x)
                if (pred(pix[static_cast<std::size_t>(y) * kBBW + x]))
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
        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);

        // ---- Check A: canonical custom viewport (positioning + all-around clipping guard) -------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            const int lx = 5, ly = 4, lw = 17, lh = 11;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(lx, ly, lw, lh), red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);

            int outside = 0;
            for (int y = 0; y < kBBH; ++y)
                for (int x = 0; x < kBBW; ++x)
                    if (Redish(At(pix, x, y)) &&
                        (x < vpX || x >= vpX + vpW || y < vpY || y >= vpY + vpH))
                        ++outside;

            check(!b.empty(), "A0: custom-viewport backbuffer rendered Red: " + b.str());
            check(CloseTo(b.width(), lw, 1) && CloseTo(b.height(), lh, 1),
                  "A1: footprint 17x11 (1:1, not squished): " + b.str());
            check(CloseTo(b.minX, vpX + lx, 1) && CloseTo(b.minY, vpY + ly, 1),
                  "A2: footprint origin == (V.X+localX, V.Y+localY) = (24,15): " + b.str());
            check(outside == 0, "A3: no red outside the viewport rectangle: outside=" + std::to_string(outside));
            check(Redish(At(pix, 32, 20)) && !Redish(At(pix, 23, 20)),
                  "A4: interior probe red, just-left-of-footprint black");
        }

        // ---- Check B: default full viewport is unregressed (fix is a no-op) ---------------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(14, 10, 34, 22), red);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b.width(), 34, 1) && CloseTo(b.height(), 22, 1),
                  "B1: default-viewport footprint 34x22: " + b.str());
            check(CloseTo(b.minX, 14, 1) && CloseTo(b.minY, 10, 1),
                  "B2: default-viewport footprint at (14,10): " + b.str());
        }

        // ---- Check C: transformMatrix (scale 1.5 + translate 2,3) + custom viewport -------------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                Matrix transform = Matrix::getIdentityProperty();
                transform.M11 = 1.5f; transform.M22 = 1.5f;
                transform.M41 = 2.0f; transform.M42 = 3.0f;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
                         nullptr, transform);
                sb.Draw(whiteTex_, Rectangle(4, 2, 8, 6), Rectangle(0, 0, 1, 1), red);
                sb.End();
            }
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b.width(), 12, 1) && CloseTo(b.height(), 9, 1),
                  "C1: transform+custom-viewport footprint 12x9 (viewport-local, W/H-normalized): " + b.str());
            check(CloseTo(b.minX, vpX + 8, 1) && CloseTo(b.minY, vpY + 6, 1),
                  "C2: transform+custom-viewport footprint at (27,17): " + b.str());
        }

        // ---- Check D: two viewports in one frame, each batch uses its own live viewport ---------
        {
            dev.Clear(Color::Black);
            SetVp(dev, 5, 5, 30, 30);
            DrawSprite(dev, Rectangle(2, 2, 10, 10), red);      // A: -> (7,7) 10x10
            SetVp(dev, 50, 40, 30, 25);
            DrawSprite(dev, Rectangle(3, 3, 12, 8), green);     // B: -> (53,43) 12x8
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox rb = Box(pix, Redish);
            const BBox gb = Box(pix, Greenish);
            check(CloseTo(rb.minX, 7, 1) && CloseTo(rb.minY, 7, 1) && CloseTo(rb.width(), 10, 1) && CloseTo(rb.height(), 10, 1),
                  "D1: viewport A red 10x10 at (7,7): " + rb.str());
            check(CloseTo(gb.minX, 53, 1) && CloseTo(gb.minY, 43, 1) && CloseTo(gb.width(), 12, 1) && CloseTo(gb.height(), 8, 1),
                  "D2: viewport B green 12x8 at (53,43): " + gb.str());
        }

        // ---- Check E: a sprite overflowing all four edges is clipped to the viewport rect -------
        {
            const int vpX = 20, vpY = 15, vpW = 30, vpH = 20;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(-4, -3, 38, 26), red);   // overflows every side
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            int outside = 0;
            for (int y = 0; y < kBBH; ++y)
                for (int x = 0; x < kBBW; ++x)
                    if (Redish(At(pix, x, y)) &&
                        (x < vpX || x >= vpX + vpW || y < vpY || y >= vpY + vpH))
                        ++outside;
            check(CloseTo(b.minX, vpX, 0) && CloseTo(b.minY, vpY, 0) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 0) && CloseTo(b.maxY, vpY + vpH - 1, 0),
                  "E1: overflowing sprite clipped to exactly the viewport rect [20,49]x[15,34]: " + b.str());
            check(outside == 0, "E2: nothing rendered outside the viewport: outside=" + std::to_string(outside));
        }

        // ---- Check F: Viewport W/H define extent, they do NOT rescale 1:1 sprite pixels ---------
        {
            SetVp(dev, 10, 10, 50, 40);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(5, 4, 17, 11), red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b1 = Box(ReadBackbuffer(dev), Redish);

            SetVp(dev, 10, 10, 30, 25);   // smaller viewport, same sprite dest
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(5, 4, 17, 11), red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b2 = Box(ReadBackbuffer(dev), Redish);

            check(CloseTo(b1.width(), 17, 1) && CloseTo(b1.height(), 11, 1) &&
                  CloseTo(b1.minX, 15, 1) && CloseTo(b1.minY, 14, 1),
                  "F1: 50x40 viewport -> 17x11 at (15,14): " + b1.str());
            check(CloseTo(b2.width(), 17, 1) && CloseTo(b2.height(), 11, 1) &&
                  CloseTo(b2.minX, 15, 1) && CloseTo(b2.minY, 14, 1),
                  "F2: 30x25 viewport -> SAME 17x11 at (15,14) (not rescaled by W/H): " + b2.str());
        }

        // ---- Check G: negative/partially-offscreen viewport-local rect clips at Viewport.X ------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(-5, 3, 20, 10), red);    // local x in [-5,15)
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            int leftLeak = 0;
            for (int y = 0; y < kBBH; ++y)
                for (int x = 0; x < vpX; ++x)
                    if (Redish(At(pix, x, y))) ++leftLeak;
            check(CloseTo(b.minX, vpX, 0) && CloseTo(b.maxX, 33, 1) &&
                  CloseTo(b.minY, vpY + 3, 1) && CloseTo(b.maxY, vpY + 12, 1),
                  "G1: left-clipped footprint x[19,33] y[14,23]: " + b.str());
            check(leftLeak == 0, "G2: no red left of Viewport.X: leak=" + std::to_string(leftLeak));
        }

        // ---- Check H: custom Viewport is relative to the bound RenderTarget2D (a different-size
        //      framebuffer), not cached backbuffer dims. Read the RT via GetBackBufferData while it
        //      is still bound (Software GetBackBufferData reads the current target). --------------
        {
            const int rtW = 48, rtH = 40;
            const int vpX = 8, vpY = 6, vpW = 28, vpH = 22;
            const int lx = 3, ly = 2, lw = 15, lh = 12;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(lx, ly, lw, lh), red);

            std::vector<Color> pix(static_cast<std::size_t>(rtW) * rtH, Color(0, 0, 0, 0));
            const Rectangle whole(0, 0, rtW, rtH);
            dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));

            int minX = rtW, minY = rtH, maxX = -1, maxY = -1, count = 0, outside = 0;
            for (int y = 0; y < rtH; ++y)
                for (int x = 0; x < rtW; ++x)
                    if (Redish(pix[static_cast<std::size_t>(y) * rtW + x]))
                    {
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                        ++count;
                        if (x < vpX || x >= vpX + vpW || y < vpY || y >= vpY + vpH) ++outside;
                    }
            const std::string s = "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);

            check(count > 0 && CloseTo(maxX - minX + 1, lw, 1) && CloseTo(maxY - minY + 1, lh, 1) &&
                  CloseTo(minX, vpX + lx, 1) && CloseTo(minY, vpY + ly, 1),
                  "H1: RT-bound custom viewport -> 15x12 at (11,8) relative to the 48x40 RT: " + s);
            check(outside == 0, "H2: no red outside the RT viewport rect: outside=" + std::to_string(outside));
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareSpriteBatchViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareSpriteBatchViewportTest game;
    game.Run();
    return game.getResult();
}
