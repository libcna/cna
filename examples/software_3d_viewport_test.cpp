// SPDX-License-Identifier: MS-PL
// REMED-GFX-079: the Software (CPU-raster) backend's 3D draw path (DrawColoredPrimitives /
// DrawIndexedColoredPrimitives / DrawPrimitivesEx / DrawIndexedPrimitivesEx) must honor a custom
// GraphicsDevice.Viewport, exactly like the GPU backends and like the SpriteBatch path fixed by
// REMED-GFX-073.
//
// XNA/FNA contract (FNA Viewport.Project, verbatim): a post-perspective-divide NDC position is
// mapped into framebuffer pixels by
//     screenX = ((ndcX + 1) * 0.5) * Viewport.Width  + Viewport.X
//     screenY = ((-ndcY + 1) * 0.5) * Viewport.Height + Viewport.Y
//     depth   = ndcZ * (Viewport.MaxDepth - Viewport.MinDepth) + Viewport.MinDepth
// and rasterization is confined to the viewport rectangle (intersected with the framebuffer).
//
// Pre-fix Software signature (this test FAILS pre-fix): ClipVertexToRasterVertex mapped NDC over
// the FULL current framebuffer (GetViewportSize) with NO Viewport.X/Y offset, NO Width/Height
// sub-scale, NO viewport clip, and NO MinDepth/MaxDepth remap (depth = ndcZ). So a custom
// sub-viewport was silently ignored: a full-NDC quad covered the whole framebuffer regardless of
// the viewport, depth ordering ignored the depth range, and geometry leaked outside the viewport.
//
// All geometry uses an identity World*View*Projection, so clip.W == 1 and NDC == the raw vertex
// position -- every vertex lands at an exactly computable screen pixel, no projection matrix needed.
//
// Testing strategy: read the whole 96x72 backbuffer (or the bound RT) once per check via
// GetBackBufferData (the Software backend serves this from its real CPU framebuffer), then
// bounding-box / probe on the CPU. Only binary/full-intensity colors are used for spatial
// assertions so no encoding can masquerade as a coordinate error.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
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
    bool Redish(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool Greenish(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool Bluish(const Color& c)   { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }

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

class Software3DViewportTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D greenTex_;
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

    void SetVpDepth(GraphicsDevice& dev, int x, int y, int w, int h, float minD, float maxD)
    {
        Viewport vp(x, y, w, h);
        vp.setMinDepthProperty(minD);
        vp.setMaxDepthProperty(maxD);
        dev.setViewportProperty(vp);
    }

    // A solid-color NDC quad (two triangles). CullNone -> winding-agnostic. NDC == raw position
    // (identity WVP), so ndc[minX,maxX] x [minY,maxY] at depth z maps straight through the viewport
    // transform under test.
    void DrawQuad(GraphicsDevice& dev, float ndcMinX, float ndcMaxX, float ndcMinY, float ndcMaxY,
                  float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(ndcMinX, ndcMaxY, z), color }, // TL
            { Vector3(ndcMaxX, ndcMaxY, z), color }, // TR
            { Vector3(ndcMaxX, ndcMinY, z), color }, // BR
            { Vector3(ndcMinX, ndcMaxY, z), color }, // TL
            { Vector3(ndcMaxX, ndcMinY, z), color }, // BR
            { Vector3(ndcMinX, ndcMinY, z), color }, // BL
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(verts, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Same full-NDC quad via the indexed path (DrawIndexedColoredPrimitives) -- proves the indexed
    // and non-indexed 3D entry points share the viewport transform.
    void DrawQuadIndexed(GraphicsDevice& dev, float ndcMinX, float ndcMaxX, float ndcMinY, float ndcMaxY,
                         float z, const Color& color)
    {
        const VertexPositionColor verts[4] = {
            { Vector3(ndcMinX, ndcMaxY, z), color }, // 0 TL
            { Vector3(ndcMaxX, ndcMaxY, z), color }, // 1 TR
            { Vector3(ndcMaxX, ndcMinY, z), color }, // 2 BR
            { Vector3(ndcMinX, ndcMinY, z), color }, // 3 BL
        };
        VertexBuffer vb(dev, 4);
        vb.SetData(verts, 4);
        IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        ib.SetData(indices, 0, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.SetIndexBuffer(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetIndexBuffer(nullptr);
    }

    // A textured full-NDC quad through DrawPrimitivesEx (BasicEffect with a texture) -- proves the
    // effect/textured 3D path honors the viewport too (shared ClipVertexToRasterVertex).
    void DrawTexturedQuad(GraphicsDevice& dev, Texture2D& tex)
    {
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.5f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
        };
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 6);
        BasicEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setTextureEnabledProperty(true);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // One triangle at the current RasterizerState (used by the culling/winding check). NDC == raw.
    void DrawTriangle(GraphicsDevice& dev, const Vector3& a, const Vector3& b, const Vector3& c,
                      const Color& color)
    {
        const VertexPositionColor verts[3] = {{a, color}, {b, color}, {c, color}};
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

    std::vector<Color> ReadWhole(GraphicsDevice& dev, int w, int h)
    {
        std::vector<Color> pix(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static const Color& At(const std::vector<Color>& pix, int w, int x, int y)
    {
        return pix[static_cast<std::size_t>(y) * w + x];
    }

    template <typename Pred>
    static BBox Box(const std::vector<Color>& pix, int w, int h, Pred pred)
    {
        BBox b;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (pred(pix[static_cast<std::size_t>(y) * w + x]))
                {
                    b.minX = std::min(b.minX, x); b.maxX = std::max(b.maxX, x);
                    b.minY = std::min(b.minY, y); b.maxY = std::max(b.maxY, y);
                    ++b.count;
                }
        return b;
    }

    template <typename Pred>
    static int CountOutside(const std::vector<Color>& pix, int w, int h, Pred pred,
                            int vpX, int vpY, int vpW, int vpH)
    {
        int outside = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (pred(pix[static_cast<std::size_t>(y) * w + x]) &&
                    (x < vpX || x >= vpX + vpW || y < vpY || y >= vpY + vpH))
                    ++outside;
        return outside;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        greenTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);
        const Color blue(0, 0, 255, 255);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Quads are authored winding-agnostic; disable culling for the coverage/clip checks (the
        // dedicated culling check L flips this back on).
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // ---- A: canonical custom Viewport(19,11,41,29), full-NDC quad -> exactly the viewport rect
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            const int outside = CountOutside(pix, kBBW, kBBH, Redish, vpX, vpY, vpW, vpH);
            check(!b.empty(), "A0: custom-viewport 3D quad rendered Red: " + b.str());
            check(CloseTo(b.minX, vpX, 1) && CloseTo(b.minY, vpY, 1) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 1) && CloseTo(b.maxY, vpY + vpH - 1, 1),
                  "A1: full-NDC quad fills exactly the viewport rect x[19,59] y[11,39]: " + b.str());
            check(outside == 0, "A2: no red outside the viewport rectangle: outside=" + std::to_string(outside));
            check(Redish(At(pix, kBBW, 39, 25)) && !Redish(At(pix, kBBW, 10, 25)),
                  "A3: interior probe red, just-left-of-viewport black");
        }

        // ---- B: default full Viewport(0,0,96,72), inset NDC[-0.5,0.5] quad (regression guard) ----
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, red);
            const BBox b = Box(ReadWhole(dev, kBBW, kBBH), kBBW, kBBH, Redish);
            // x=-0.5 -> 24, x=0.5 -> 72 ; y=0.5 -> 18, y=-0.5 -> 54
            check(CloseTo(b.minX, 24, 1) && CloseTo(b.minY, 18, 1) &&
                  CloseTo(b.width(), 48, 1) && CloseTo(b.height(), 36, 1),
                  "B1: default-viewport 3D quad unregressed 48x36 at (24,18): " + b.str());
        }

        // ---- C: Width/Height-only discriminator, origin (0,0), Viewport(0,0,48,36) ---------------
        {
            const int vpX = 0, vpY = 0, vpW = 48, vpH = 36;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            const int outside = CountOutside(pix, kBBW, kBBH, Redish, vpX, vpY, vpW, vpH);
            check(CloseTo(b.minX, 0, 1) && CloseTo(b.minY, 0, 1) &&
                  CloseTo(b.maxX, vpW - 1, 1) && CloseTo(b.maxY, vpH - 1, 1),
                  "C1: W/H sub-scale -> quad fills [0,47]x[0,35] not the full framebuffer: " + b.str());
            check(outside == 0, "C2: no red outside the 48x36 viewport: outside=" + std::to_string(outside));
        }

        // ---- D: X/Y-only discriminator (same size, shifted origin -> geometry translates) --------
        {
            SetVp(dev, 0, 0, 40, 30);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b1 = Box(ReadWhole(dev, kBBW, kBBH), kBBW, kBBH, Redish);

            SetVp(dev, 25, 20, 40, 30);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b2 = Box(ReadWhole(dev, kBBW, kBBH), kBBW, kBBH, Redish);

            check(CloseTo(b1.minX, 0, 1) && CloseTo(b1.minY, 0, 1) &&
                  CloseTo(b2.minX, 25, 1) && CloseTo(b2.minY, 20, 1),
                  "D1: Viewport.X/Y offset applied (origin (0,0)->(25,20)): " + b1.str() + " vs " + b2.str());
            check(CloseTo(b2.minX - b1.minX, 25, 1) && CloseTo(b2.minY - b1.minY, 20, 1) &&
                  CloseTo(b1.width(), b2.width(), 1) && CloseTo(b1.height(), b2.height(), 1),
                  "D2: identical 40x30 extent shifted by exactly (25,20): dW=" +
                  std::to_string(b2.width() - b1.width()) + " dH=" + std::to_string(b2.height() - b1.height()));
        }

        // ---- E: geometry overflowing all four viewport edges is clipped to the viewport rect -----
        {
            const int vpX = 20, vpY = 15, vpW = 30, vpH = 20;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.5f, 1.5f, -1.5f, 1.5f, 0.5f, red);  // NDC extends past [-1,1] on all sides
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            const int outside = CountOutside(pix, kBBW, kBBH, Redish, vpX, vpY, vpW, vpH);
            check(CloseTo(b.minX, vpX, 0) && CloseTo(b.minY, vpY, 0) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 0) && CloseTo(b.maxY, vpY + vpH - 1, 0),
                  "E1: overflowing 3D quad clipped to exactly the viewport rect [20,49]x[15,34]: " + b.str());
            check(outside == 0, "E2: no 3D pixels rendered outside the viewport: outside=" + std::to_string(outside));
        }

        // ---- F: Y convention -- top-half NDC maps to the TOP of the viewport, not the bottom ------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, 0.0f, 1.0f, 0.5f, red);  // NDC y in [0,1] -> screen y[11,25]
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            check(CloseTo(b.minY, vpY, 1) && b.maxY <= 27,
                  "F1: top-half NDC lands in the top of the viewport (no vertical mirror): " + b.str());
            check(Redish(At(pix, kBBW, 39, 13)) && !Redish(At(pix, kBBW, 39, 37)),
                  "F2: red at viewport-top probe, black at viewport-bottom probe");
        }

        // ---- G: viewport is relative to the bound RenderTarget2D (a different-size framebuffer) ---
        {
            const int rtW = 48, rtH = 40;
            const int vpX = 7, vpY = 5, vpW = 27, vpH = 21;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            const std::vector<Color> pix = ReadWhole(dev, rtW, rtH);
            const BBox b = Box(pix, rtW, rtH, Redish);
            const int outside = CountOutside(pix, rtW, rtH, Redish, vpX, vpY, vpW, vpH);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);
            // x[-1,1] -> [7,34) = pixels [7,33]; y[1,-1] -> [5,26) = pixels [5,25], relative to the RT.
            check(!b.empty() && CloseTo(b.minX, vpX, 1) && CloseTo(b.minY, vpY, 1) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 1) && CloseTo(b.maxY, vpY + vpH - 1, 1),
                  "G1: RT-bound custom viewport fills [7,33]x[5,25] relative to the 48x40 RT: " + b.str());
            check(outside == 0, "G2: no red outside the RT viewport rect: outside=" + std::to_string(outside));
        }

        // ---- H: MinDepth/MaxDepth remap -- compressing MaxDepth pulls a "far" quad in front -------
        {
            // Clear the depth buffer to 1.0 while a default (MaxDepth=1) viewport is active, then draw
            // a RED quad at z=0.9 under MaxDepth=0.4 -> stored depth 0.36, and a BLUE quad at z=0.5
            // under the default range -> stored depth 0.5. With Less depth testing the RED (0.36) wins.
            // Pre-fix (no remap): RED stores 0.9, BLUE 0.5 -> BLUE wins.
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            SetVpDepth(dev, 0, 0, kBBW, kBBH, 0.0f, 0.4f);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.9f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);   // MinDepth=0, MaxDepth=1
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, blue);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            check(Redish(At(pix, kBBW, 48, 36)) && !Bluish(At(pix, kBBW, 48, 36)),
                  "H1: MaxDepth=0.4 pulls the z=0.9 red quad in front of the z=0.5 blue quad (center red)");
        }

        // ---- I: depth ordering stays correct inside a custom viewport (both draw orders) ----------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            const int cx = 39, cy = 25;  // inside the viewport
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.8f, blue);  // far
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.2f, red);   // near
            SetVp(dev, 0, 0, kBBW, kBBH);
            const bool nearWins1 = Redish(At(ReadWhole(dev, kBBW, kBBH), kBBW, cx, cy));

            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.2f, red);   // near first
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.8f, blue);  // far second
            SetVp(dev, 0, 0, kBBW, kBBH);
            const bool nearWins2 = Redish(At(ReadWhole(dev, kBBW, kBBH), kBBW, cx, cy));

            check(nearWins1 && nearWins2,
                  "I1: near(red) beats far(blue) in a custom viewport, order-independent");
        }

        // ---- J: indexed draw honors the viewport identically to the non-indexed draw --------------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuadIndexed(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            const int outside = CountOutside(pix, kBBW, kBBH, Redish, vpX, vpY, vpW, vpH);
            check(CloseTo(b.minX, vpX, 1) && CloseTo(b.minY, vpY, 1) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 1) && CloseTo(b.maxY, vpY + vpH - 1, 1) && outside == 0,
                  "J1: DrawIndexedPrimitives fills the same viewport rect x[19,59] y[11,39]: " + b.str());
        }

        // ---- K: two viewports in one frame, each draw uses the live viewport ----------------------
        {
            dev.Clear(Color::Black);
            SetVp(dev, 5, 5, 30, 25);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);    // -> x[5,34] y[5,29]
            SetVp(dev, 55, 40, 30, 25);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, green);  // -> x[55,84] y[40,64]
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox rb = Box(pix, kBBW, kBBH, Redish);
            const BBox gb = Box(pix, kBBW, kBBH, Greenish);
            check(CloseTo(rb.minX, 5, 1) && CloseTo(rb.minY, 5, 1) &&
                  CloseTo(rb.maxX, 34, 1) && CloseTo(rb.maxY, 29, 1),
                  "K1: viewport A red fills x[5,34] y[5,29]: " + rb.str());
            check(CloseTo(gb.minX, 55, 1) && CloseTo(gb.minY, 40, 1) &&
                  CloseTo(gb.maxX, 84, 1) && CloseTo(gb.maxY, 64, 1),
                  "K2: viewport B green fills x[55,84] y[40,64]: " + gb.str());
        }

        // ---- L: viewport transform preserves triangle winding (culling still discriminates) -------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            // Order (a,c,b) is front-facing under the real default (CullCounterClockwiseFace): it has
            // a negative screen-space area after the viewport transform, so it survives culling. The
            // viewport transform (translate + positive-scale) is orientation-preserving, so this
            // winding sign is identical to the full-framebuffer mapping -- the fix must not flip it.
            const Vector3 a( 0.0f,  0.8f, 0.5f);
            const Vector3 b(-0.8f, -0.8f, 0.5f);
            const Vector3 c( 0.8f, -0.8f, 0.5f);

            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
            DrawTriangle(dev, a, c, b, red);          // front-facing -> visible
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b1 = Box(pix, kBBW, kBBH, Redish);
            const int outside = CountOutside(pix, kBBW, kBBH, Redish, vpX, vpY, vpW, vpH);

            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawTriangle(dev, a, b, c, red);          // reversed winding -> culled
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b2 = Box(ReadWhole(dev, kBBW, kBBH), kBBW, kBBH, Redish);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            check(!b1.empty() && Redish(At(pix, kBBW, 39, 28)) && outside == 0,
                  "L1: front-facing triangle survives the viewport transform (winding preserved): " + b1.str());
            check(b2.empty(),
                  "L2: reversed-winding triangle is still culled under the viewport transform: " + b2.str());
        }

        // ---- M: textured 3D quad (DrawPrimitivesEx) honors the viewport ---------------------------
        {
            const int vpX = 19, vpY = 11, vpW = 41, vpH = 29;
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawTexturedQuad(dev, greenTex_);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Greenish);
            const int outside = CountOutside(pix, kBBW, kBBH, Greenish, vpX, vpY, vpW, vpH);
            check(CloseTo(b.minX, vpX, 1) && CloseTo(b.minY, vpY, 1) &&
                  CloseTo(b.maxX, vpX + vpW - 1, 1) && CloseTo(b.maxY, vpY + vpH - 1, 1) && outside == 0,
                  "M1: textured 3D quad confined to the viewport rect x[19,59] y[11,39]: " + b.str());
        }

        // ---- N: partially-offscreen viewport clips safely to the framebuffer (no OOB) -------------
        {
            const int vpX = 80, vpY = 60, vpW = 40, vpH = 30;  // extends to (120,90), past 96x72
            SetVp(dev, vpX, vpY, vpW, vpH);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadWhole(dev, kBBW, kBBH);
            const BBox b = Box(pix, kBBW, kBBH, Redish);
            // Clipped to framebuffer ∩ viewport = x[80,95] y[60,71].
            check(!b.empty() && b.minX >= vpX && b.minY >= vpY && b.maxX <= kBBW - 1 && b.maxY <= kBBH - 1,
                  "N1: partially-offscreen viewport clips inside the framebuffer, no OOB: " + b.str());
        }

        // ---- O: zero-size viewport draws nothing and does not crash -------------------------------
        {
            SetVp(dev, 10, 10, 0, 20);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b = Box(ReadWhole(dev, kBBW, kBBH), kBBW, kBBH, Redish);
            check(b.empty(), "O1: zero-width viewport rasterizes nothing (no crash, no NaN): " + b.str());
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    Software3DViewportTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    Software3DViewportTest game;
    game.Run();
    return game.getResult();
}
