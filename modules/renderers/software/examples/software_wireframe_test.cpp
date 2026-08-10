// SPDX-License-Identifier: MS-PL
// REMED-GFX-082: the Software (CPU-raster) renderer must honor RasterizerState.FillMode. Before this
// task SoftwareRenderer::ApplyRasterizerState dropped its fillMode argument and the rasterizer
// only ever filled solid triangles, so FillMode.WireFrame rendered as a solid fill (no line-raster
// path existed). This is the Software counterpart of the GPU renderers' wireframe support (e.g. EasyGL
// expands each triangle into GL_LINES; D3D11/Vulkan use the hardware WIREFRAME fill mode).
//
// XNA/FNA contract (verified against FNA RasterizerState.FillMode + PipelineCache):
//   FillMode.Solid     -> normal filled triangle rasterization.
//   FillMode.WireFrame -> rasterize only the triangle EDGES (no interior fragments).
// FillMode is applied AFTER primitive assembly and culling: a culled triangle produces no wire edges,
// and wire fragments still participate in depth test/write, viewport, and scissor clipping exactly
// like solid fragments. The RasterizerState applies to ALL submitted triangle geometry, including the
// two triangles SpriteBatch generates per sprite (so a wireframe sprite shows its quad outline plus
// the internal triangle-split diagonal -- real submitted geometry, matching D3D11/FNA).
//
// Pre-fix Software behavior (this test's WireFrame checks FAIL pre-fix): FillMode ignored ->
// FillMode.WireFrame renders a SOLID fill, so the triangle interior is filled instead of empty.
//
// Testing strategy (identical to GFX-073/079/080): render into the 96x72 backbuffer (or a bound RT),
// read the whole target once per check via GetBackBufferData (served from the real CPU framebuffer),
// then bounding-box / probe on the CPU. Only binary/full-intensity colors are used for spatial
// assertions; edge probes tolerate +/-2px so they never depend on an ambiguous single tie pixel.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <cmath>
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

    // The shared "big triangle" in NDC (identity WVP -> NDC == raw position). Screen-space corners
    // under the default full 96x72 viewport: v0(19.2,57.6) v1(76.8,57.6) v2(48,14.4); centroid
    // (48,43.2). Screen area is POSITIVE, so it is culled by CullCounterClockwiseFace (the default)
    // and kept by CullClockwiseFace / CullNone -- used by the culling section.
    constexpr float TX0 = -0.6f, TY0 = -0.6f;
    constexpr float TX1 =  0.6f, TY1 = -0.6f;
    constexpr float TX2 =  0.0f, TY2 =  0.6f;

    bool isRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool isGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool isBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }
    bool anyLit(const Color& c)  { return c.getRProperty() >= 200 || c.getGProperty() >= 200 || c.getBProperty() >= 200; }

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    struct BBox
    {
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        int count = 0;
        bool empty() const { return count == 0; }
        std::string str() const
        {
            if (empty()) return "<empty>";
            return "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                 + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);
        }
    };
}

class SoftwareWireframeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    Texture2D greenTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;
    int solidColoredCount_ = 0;   // section A -> reused as the wireframe "much fewer pixels" oracle

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

    // Assigns a RasterizerState with the given cull/fill/scissor to the device (the 3D draw paths
    // read GraphicsDevice.RasterizerState directly). Returns the state so SpriteBatch checks can pass
    // the SAME state through Begin (REMED-GFX-081).
    RasterizerState MakeRaster(CullMode cull, FillMode fill, bool scissor = false)
    {
        RasterizerState rs;
        rs.setCullModeProperty(cull);
        rs.setFillModeProperty(fill);
        rs.setScissorTestEnableProperty(scissor);
        return rs;
    }
    void SetRaster(GraphicsDevice& dev, CullMode cull, FillMode fill, bool scissor = false)
    {
        dev.setRasterizerStateProperty(MakeRaster(cull, fill, scissor));
    }

    // ---- draw helpers: one triangle / quad through each renderer entry point ----

    // DrawColoredPrimitives (via DrawUserPrimitives<VertexPositionColor>).
    void TriColored(GraphicsDevice& dev, const Color& c, float z = 0.5f)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(TX0, TY0, z), c }, { Vector3(TX1, TY1, z), c }, { Vector3(TX2, TY2, z), c },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    // Per-vertex colored triangle (attribute-interpolation section).
    void TriColoredVerts(GraphicsDevice& dev, const Color& c0, const Color& c1, const Color& c2)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(TX0, TY0, 0.5f), c0 }, { Vector3(TX1, TY1, 0.5f), c1 }, { Vector3(TX2, TY2, 0.5f), c2 },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    // DrawIndexedColoredPrimitives (via DrawUserIndexedPrimitives<VertexPositionColor>).
    void TriIndexedColored(GraphicsDevice& dev, const Color& c)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(TX0, TY0, 0.5f), c }, { Vector3(TX1, TY1, 0.5f), c }, { Vector3(TX2, TY2, 0.5f), c },
        };
        const std::uint16_t idx[3] = {0, 1, 2};
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts, 0, 3, idx, 0, 1);
    }

    // DrawPrimitivesEx (VertexBuffer + BasicEffect.VertexColorEnabled -> DrawPrimitives).
    void TriEx(GraphicsDevice& dev, const Color& c, float z = 0.5f)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(TX0, TY0, z), c }, { Vector3(TX1, TY1, z), c }, { Vector3(TX2, TY2, z), c },
        };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

    // DrawIndexedPrimitivesEx (VertexBuffer + IndexBuffer -> DrawIndexedPrimitives).
    void TriIndexedEx(GraphicsDevice& dev, const Color& c)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(TX0, TY0, 0.5f), c }, { Vector3(TX1, TY1, 0.5f), c }, { Vector3(TX2, TY2, 0.5f), c },
        };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
        const std::uint16_t idx[3] = {0, 1, 2};
        ib.SetData(idx, 0, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.SetIndexBuffer(&ib);
        dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
        dev.SetVertexBuffer(nullptr);
        dev.SetIndexBuffer(nullptr);
    }

    // Textured DrawPrimitivesEx (BasicEffect with a texture) using the big triangle's UVs.
    void TriTexturedEx(GraphicsDevice& dev, Texture2D& tex)
    {
        const VertexPositionTexture verts[3] = {
            { Vector3(TX0, TY0, 0.5f), Vector2(0.0f, 1.0f) },
            { Vector3(TX1, TY1, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(TX2, TY2, 0.5f), Vector2(0.5f, 0.0f) },
        };
        VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setTextureEnabledProperty(true);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
    }

    // A full-NDC solid quad at depth z (depth-interaction controls).
    void SolidQuad(GraphicsDevice& dev, float z, const Color& c)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), c }, { Vector3( 1.0f,  1.0f, z), c }, { Vector3( 1.0f, -1.0f, z), c },
            { Vector3(-1.0f,  1.0f, z), c }, { Vector3( 1.0f, -1.0f, z), c }, { Vector3(-1.0f, -1.0f, z), c },
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

    // ---- readback helpers ----

    std::vector<Color> Read(GraphicsDevice& dev, int w = kBBW, int h = kBBH)
    {
        std::vector<Color> pix(static_cast<std::size_t>(w) * h, Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, w, h);
        dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));
        return pix;
    }

    static const Color& At(const std::vector<Color>& pix, int x, int y, int w = kBBW)
    {
        return pix[static_cast<std::size_t>(y) * w + x];
    }

    template <typename Pred>
    static BBox Box(const std::vector<Color>& pix, Pred pred, int w = kBBW, int h = kBBH)
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

    // Any pixel matching pred within the inclusive [cx-r,cx+r] x [cy-r,cy+r] window (clamped to fb).
    template <typename Pred>
    static bool LitNear(const std::vector<Color>& pix, int cx, int cy, int r, Pred pred,
                        int w = kBBW, int h = kBBH)
    {
        for (int y = std::max(0, cy - r); y <= std::min(h - 1, cy + r); ++y)
            for (int x = std::max(0, cx - r); x <= std::min(w - 1, cx + r); ++x)
                if (pred(At(pix, x, y, w))) return true;
        return false;
    }

    // Count pixels matching pred inside the inclusive rectangle [x0,x1] x [y0,y1].
    template <typename Pred>
    static int CountInRect(const std::vector<Color>& pix, Pred pred, int x0, int y0, int x1, int y1,
                           int w = kBBW)
    {
        int n = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (pred(At(pix, x, y, w))) ++n;
        return n;
    }

    // Count pixels matching pred OUTSIDE the inclusive rectangle [x0,x1] x [y0,y1].
    template <typename Pred>
    static int OutsideRect(const std::vector<Color>& pix, Pred pred, int x0, int y0, int x1, int y1,
                           int w = kBBW, int h = kBBH)
    {
        int n = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (pred(At(pix, x, y, w)) && (x < x0 || x > x1 || y < y0 || y > y1)) ++n;
        return n;
    }

    // The shared wireframe geometry probe: v0(19,58) v1(77,58) v2(48,14); edge midpoints
    // bottom(48,57) left(33,36) right(62,36). Interior box [40,56]x[34,48] is entirely off every edge.
    template <typename Pred>
    void CheckWireGeometry(const std::vector<Color>& pix, Pred pred, const std::string& tag)
    {
        const bool edges = LitNear(pix, 48, 57, 2, pred) &&   // bottom edge midpoint
                           LitNear(pix, 33, 36, 2, pred) &&   // left edge midpoint
                           LitNear(pix, 62, 36, 2, pred);     // right edge midpoint
        check(edges, tag + ": all three triangle edges are drawn (bottom/left/right midpoints lit)");
        const bool corners = LitNear(pix, 19, 58, 3, pred) &&
                             LitNear(pix, 77, 58, 3, pred) &&
                             LitNear(pix, 48, 14, 3, pred);
        check(corners, tag + ": edges reach all three triangle corners");
        const int interior = CountInRect(pix, pred, 40, 34, 56, 48);
        check(interior == 0, tag + ": triangle interior is empty (no fill) -- " +
              std::to_string(interior) + " lit interior pixels");
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
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

        dev.setDepthStencilStateProperty(DepthStencilState::None);
        SetVp(dev, 0, 0, kBBW, kBBH);

        // ===== A (Phase 5): SOLID control -- filled triangle, byte-for-byte the pre-fix result =====
        {
            SetRaster(dev, CullMode::None, FillMode::Solid);
            dev.Clear(Color::Black);
            TriColored(dev, green);
            const std::vector<Color> pix = Read(dev);
            const BBox b = Box(pix, isGreen);
            solidColoredCount_ = b.count;
            check(isGreen(At(pix, 48, 43)), "A1: SOLID triangle interior center (48,43) is filled green");
            check(b.count > 900, "A2: SOLID triangle fills a large area (~1244px): " + b.str());
            check(CloseTo(b.minX, 19, 2) && CloseTo(b.maxX, 77, 2) &&
                  CloseTo(b.minY, 14, 2) && CloseTo(b.maxY, 58, 2),
                  "A3: SOLID triangle bounding box == the triangle screen extent: " + b.str());
        }

        // ===== B (Phase 4/17): WIREFRAME colored (DrawColoredPrimitives) -- edges only =============
        {
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriColored(dev, green);
            const std::vector<Color> pix = Read(dev);
            const BBox b = Box(pix, isGreen);
            check(!isGreen(At(pix, 48, 43)), "B1: WIREFRAME triangle center (48,43) is NOT filled (background)");
            CheckWireGeometry(pix, isGreen, "B2");
            check(b.count > 100 && b.count < solidColoredCount_ / 3,
                  "B3: WIREFRAME lit count << SOLID (perimeter not area): wire=" + std::to_string(b.count) +
                  " solid=" + std::to_string(solidColoredCount_));
            check(OutsideRect(pix, isGreen, 18, 13, 78, 59) == 0, "B4: no wire pixels outside the triangle extent");
        }

        // ===== C (Phase 18): WIREFRAME indexed colored (DrawIndexedColoredPrimitives) =============
        {
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriIndexedColored(dev, green);
            const std::vector<Color> pix = Read(dev);
            check(!isGreen(At(pix, 48, 43)), "C1: indexed-colored WIREFRAME center is background (not filled)");
            CheckWireGeometry(pix, isGreen, "C2");
        }

        // ===== D (Phase 4/5): WIREFRAME + SOLID via DrawPrimitivesEx =================================
        {
            SetRaster(dev, CullMode::None, FillMode::Solid);
            dev.Clear(Color::Black);
            TriEx(dev, green);
            check(isGreen(At(Read(dev), 48, 43)), "D1: DrawPrimitivesEx SOLID center is filled green");

            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);
            const std::vector<Color> pix = Read(dev);
            check(!isGreen(At(pix, 48, 43)), "D2: DrawPrimitivesEx WIREFRAME center is background");
            CheckWireGeometry(pix, isGreen, "D3");
        }

        // ===== E (Phase 19): WIREFRAME textured DrawPrimitivesEx -- shaded edges, empty interior ====
        {
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriTexturedEx(dev, greenTex_);   // green texture modulated onto the wire edges
            const std::vector<Color> pix = Read(dev);
            check(!isGreen(At(pix, 48, 43)), "E1: textured WIREFRAME center is background");
            CheckWireGeometry(pix, isGreen, "E2");
        }

        // ===== F (Phase 20): WIREFRAME indexed DrawIndexedPrimitivesEx ==============================
        {
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriIndexedEx(dev, green);
            const std::vector<Color> pix = Read(dev);
            check(!isGreen(At(pix, 48, 43)), "F1: DrawIndexedPrimitivesEx WIREFRAME center is background");
            CheckWireGeometry(pix, isGreen, "F2");
        }

        // ===== G (Phase 11): culling happens BEFORE wireframe edge rasterization =====================
        // The big triangle has positive screen area -> culled by CullCounterClockwiseFace, kept by
        // CullClockwiseFace / None. Culling semantics must be identical to solid mode.
        {
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);
            check(Box(Read(dev), isGreen).count > 100, "G1: WIREFRAME + CullNone -> edges drawn");

            SetRaster(dev, CullMode::CullCounterClockwiseFace, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);
            check(Box(Read(dev), isGreen).count == 0,
                  "G2: WIREFRAME + CullCounterClockwiseFace -> triangle culled, NO wire edges");

            SetRaster(dev, CullMode::CullClockwiseFace, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);
            check(Box(Read(dev), isGreen).count > 100, "G3: WIREFRAME + CullClockwiseFace -> edges drawn");
        }

        // ===== H (Phase 12): WIREFRAME edges clipped to framebuffer INTERSECT Viewport ==============
        {
            SetVp(dev, 10, 8, 50, 40);   // clip x[10,59] y[8,47]
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);           // big triangle now maps into the sub-viewport
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = Read(dev);
            const BBox b = Box(pix, isGreen);
            check(b.count > 0 && b.minX >= 10 && b.maxX <= 59 && b.minY >= 8 && b.maxY <= 47,
                  "H1: WIREFRAME confined to framebuffer INTERSECT Viewport x[10,59] y[8,47]: " + b.str());
            check(OutsideRect(pix, isGreen, 10, 8, 59, 47) == 0, "H2: no wire pixels leak outside the viewport");
        }

        // ===== I (Phase 13): WIREFRAME edges clipped to Scissor (reusing GFX-080 RasterClipRect) ====
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetRaster(dev, CullMode::None, FillMode::WireFrame, /*scissor*/ true);
            dev.setScissorRectangleProperty(Rectangle(30, 25, 24, 18));   // x[30,53] y[25,42]
            dev.Clear(Color::Black);
            TriEx(dev, green);
            const std::vector<Color> pix = Read(dev);
            check(OutsideRect(pix, isGreen, 30, 25, 53, 42) == 0,
                  "I1: WIREFRAME edges clipped to the scissor rectangle (nothing outside)");
            check(Box(pix, isGreen).count > 0, "I2: some wire edge survives inside the scissor");
        }

        // ===== J (Phase 14): wire fragments participate in the DEPTH TEST (depth read) ==============
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.Clear(Color::Black);   // clears depth to 1.0
            SetRaster(dev, CullMode::None, FillMode::Solid);
            SolidQuad(dev, 0.8f, blue);                 // far blue fills everything, depth 0.8
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            TriEx(dev, green, 0.2f);                    // near green edges: 0.2 < 0.8 -> pass
            TriEx(dev, red, 0.95f);                     // behind red edges: 0.95 > 0.8 -> fail
            const std::vector<Color> pix = Read(dev);
            check(LitNear(pix, 48, 57, 2, isGreen) && LitNear(pix, 33, 36, 2, isGreen),
                  "J1: near WIREFRAME edges (depth 0.2) win the depth test over far blue");
            check(Box(pix, isRed).count == 0, "J2: behind WIREFRAME edges (depth 0.95) lose the depth test (no red)");
            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        // ===== K (Phase 15): wire fragments WRITE depth exactly like solid fragments ================
        {
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.Clear(Color::Black);
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            TriEx(dev, green, 0.2f);                    // near green edges write depth 0.2
            SetRaster(dev, CullMode::None, FillMode::Solid);
            SolidQuad(dev, 0.5f, blue);                 // far blue: 0.5 > 0.2 at edges -> loses -> green survives
            const std::vector<Color> pix = Read(dev);
            check(LitNear(pix, 48, 57, 2, isGreen) && LitNear(pix, 33, 36, 2, isGreen),
                  "K1: WIREFRAME wrote depth -> later far solid does not overwrite the near wire edges");
            check(isBlue(At(pix, 48, 43)), "K2: interior (no wire, no near depth) is filled by the later solid quad");
            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        // ===== L (Phase 24): three FillModes in one frame -- no stale fill mode ======================
        // Left third SOLID red, middle third WIREFRAME green, right third SOLID blue. Each region's
        // center must reflect its own fill mode (a leaked mode would hole-out a solid or fill a wire).
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            const auto region = [&](GraphicsDevice& d, float nx0, float nx1, FillMode fm, const Color& c) {
                const VertexPositionColor v[3] = {
                    { Vector3(nx0, -0.7f, 0.5f), c }, { Vector3(nx1, -0.7f, 0.5f), c }, { Vector3((nx0 + nx1) * 0.5f, 0.7f, 0.5f), c },
                };
                SetRaster(d, CullMode::None, fm);
                BasicEffect fx(d); fx.VertexColorEnabled = true; fx.Apply();
                d.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 1);
            };
            region(dev, -0.95f, -0.35f, FillMode::Solid,     red);    // left,  screen x ~ [2,31]
            region(dev, -0.30f,  0.30f, FillMode::WireFrame, green);  // mid,   screen x ~ [33,62]
            region(dev,  0.35f,  0.95f, FillMode::Solid,     blue);   // right, screen x ~ [64,93]
            const std::vector<Color> pix = Read(dev);
            // Region-center rows near y~43 (each triangle's centroid); midpoints of each base.
            check(Box(pix, isRed).count > 200, "L1: left SOLID red triangle is filled");
            check(Box(pix, isBlue).count > 200, "L3: right SOLID blue triangle is filled");
            const BBox gb = Box(pix, isGreen);
            check(gb.count > 40 && gb.count < 250, "L2: middle WIREFRAME green triangle is edges-only: " + gb.str());
            // The green centroid is ~ (48,43); it must be empty despite sitting between two filled solids.
            check(!isGreen(At(pix, 48, 43)) && !anyLit(At(pix, 48, 43)),
                  "L4: middle WIREFRAME centroid stays empty (fill mode is per-draw, not stale)");
        }

        // ===== M (Phase 25): default RasterizerState is Solid -- no global wireframe leak ============
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);   // default FillMode == Solid
            dev.Clear(Color::Black);
            TriEx(dev, green);
            check(isGreen(At(Read(dev), 48, 43)), "M1: default RasterizerState.FillMode == Solid (3D draw filled)");

            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                sb.Begin();                                   // no RasterizerState -> SpriteBatch default (Solid)
                sb.Draw(whiteTex_, Rectangle(20, 15, 40, 30), Rectangle(0, 0, 1, 1), red,
                        0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
                sb.End();
            }
            check(isRed(At(Read(dev), 40, 30)), "M2: default SpriteBatch.Begin() sprite is filled (Solid)");
        }

        // ===== N (Phase 22/23): SpriteBatch honors FillMode.WireFrame THROUGH Begin (GFX-081) =======
        // A large solid sprite -> two triangles (c0,c1,c2)+(c2,c3,c0). Wireframe shows the quad
        // boundary PLUS the internal split diagonal c0->c2 (real submitted geometry, like D3D11/FNA).
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            RasterizerState wireRS = MakeRaster(CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &wireRS);
                sb.Draw(whiteTex_, Rectangle(20, 15, 50, 40), Rectangle(0, 0, 1, 1), red,
                        0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);   // corners (20,15)-(70,55)
                sb.End();
            }
            const std::vector<Color> pix = Read(dev);
            const BBox b = Box(pix, isRed);
            // Boundary present (each side's midpoint) and much fewer than a full 50x40=2000 fill.
            check(LitNear(pix, 45, 15, 2, isRed) && LitNear(pix, 45, 55, 2, isRed) &&
                  LitNear(pix, 20, 35, 2, isRed) && LitNear(pix, 70, 35, 2, isRed),
                  "N1: WIREFRAME sprite draws its four quad-boundary edges");
            check(LitNear(pix, 45, 35, 2, isRed), "N2: WIREFRAME sprite shows the internal triangle-split diagonal");
            // Two interior points that are OFF the c0->c2 diagonal (y=0.8*(x-20)+15): (30,45) below, (60,25) above.
            check(!isRed(At(pix, 30, 45)) && !isRed(At(pix, 60, 25)),
                  "N3: WIREFRAME sprite interior (off the diagonal) is not filled");
            check(b.count > 60 && b.count < 700, "N4: WIREFRAME sprite is outline+diagonal, not a 2000px fill: " + b.str());
        }

        // ===== O (Phase 21): per-edge attribute interpolation (color varies along an edge) ==========
        // Bottom edge v0(19,58,red) -> v1(77,58,green): near v0 reddish, near v1 greenish.
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriColoredVerts(dev, red, green, blue);   // v0 red, v1 green, v2 blue
            const std::vector<Color> pix = Read(dev);
            // Scan a small y-window on the bottom edge for the lit pixel at two x positions.
            const auto edgeColorAt = [&](int x) -> Color {
                for (int y = 55; y <= 59; ++y) if (anyLit(At(pix, x, y))) return At(pix, x, y);
                return Color(0, 0, 0, 0);
            };
            const Color near0 = edgeColorAt(26);   // ~13% along -> red-dominant
            const Color near1 = edgeColorAt(70);   // ~88% along -> green-dominant
            check(near0.getRProperty() > near0.getGProperty() + 40,
                  "O1: near v0 the bottom edge is red-dominant (attributes interpolate, not vertex-0-for-all)");
            check(near1.getGProperty() > near1.getRProperty() + 40,
                  "O2: near v1 the bottom edge is green-dominant");
        }

        // ===== P (Phase 27/28): far-offscreen and degenerate triangles are safe =====================
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            // One vertex far off-screen; only the on-screen edge portions may be drawn, no OOB / hang.
            const VertexPositionColor big[3] = {
                { Vector3(-0.5f, -0.5f, 0.5f), green },
                { Vector3( 0.5f, -0.5f, 0.5f), green },
                { Vector3( 8.0f,  9.0f, 0.5f), green },   // maps far outside the 96x72 framebuffer
            };
            BasicEffect fx(dev); fx.VertexColorEnabled = true; fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, big, 0, 1);
            const BBox b = Box(Read(dev), isGreen);
            check(b.count >= 0 && b.minX >= 0 && b.maxX <= kBBW - 1 && b.minY >= 0 && b.maxY <= kBBH - 1,
                  "P1: far-offscreen WIREFRAME triangle stays in-bounds, no crash/OOB: " + b.str());

            // Degenerate (all identical) triangle: area==0 -> nothing drawn, no divide-by-zero.
            dev.Clear(Color::Black);
            const VertexPositionColor degen[3] = {
                { Vector3(0.0f, 0.0f, 0.5f), green }, { Vector3(0.0f, 0.0f, 0.5f), green }, { Vector3(0.0f, 0.0f, 0.5f), green },
            };
            BasicEffect fx2(dev); fx2.VertexColorEnabled = true; fx2.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, degen, 0, 1);
            check(Box(Read(dev), isGreen).count == 0, "P2: degenerate (zero-area) WIREFRAME triangle draws nothing, no UB");
        }

        // ===== Q (Phase 31): WIREFRAME into a RenderTarget2D (RT-space, no backbuffer leak) ==========
        {
            const int rtW = 64, rtH = 48;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetVp(dev, 0, 0, rtW, rtH);
            SetRaster(dev, CullMode::None, FillMode::WireFrame);
            dev.Clear(Color::Black);
            TriEx(dev, green);   // big triangle mapped over the 64x48 RT
            std::vector<Color> pix = Read(dev, rtW, rtH);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b = Box(pix, isGreen, rtW, rtH);
            // RT centroid ~ (32,29); interior must be empty, edges present, all within the RT.
            check(b.count > 0 && !isGreen(At(pix, 32, 29, rtW)),
                  "Q1: WIREFRAME into RenderTarget2D -> edges present, RT interior empty: " + b.str());
            check(b.minX >= 0 && b.maxX <= rtW - 1 && b.minY >= 0 && b.maxY <= rtH - 1,
                  "Q2: RT wireframe stays within the 64x48 RT (no backbuffer-dimension leak)");
        }

        // ===== R (Phase 32): RenderTarget + Viewport + Scissor + WIREFRAME composition ==============
        {
            const int rtW = 64, rtH = 48;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetVp(dev, 6, 4, 44, 34);                    // viewport inside the RT: clip x[6,49] y[4,37]
            SetRaster(dev, CullMode::None, FillMode::WireFrame, /*scissor*/ true);
            dev.setScissorRectangleProperty(Rectangle(14, 10, 24, 18));   // fb-space x[14,37] y[10,27]
            dev.Clear(Color::Black);
            TriEx(dev, green);
            std::vector<Color> pix = Read(dev, rtW, rtH);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);
            // Effective clip = RT INTERSECT Viewport INTERSECT Scissor = x[14,37] y[10,27].
            const int outside = OutsideRect(pix, anyLit, 14, 10, 37, 27, rtW, rtH);
            check(outside == 0, "R1: RT+Viewport+Scissor WIREFRAME confined to x[14,37] y[10,27] (nothing outside): " +
                  std::to_string(outside) + " stray");
            check(Box(pix, isGreen, rtW, rtH).count > 0, "R2: some wire edge survives the RT+Viewport+Scissor intersection");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareWireframeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareWireframeTest game;
    game.Run();
    return game.getResult();
}
