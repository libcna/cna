// SPDX-License-Identifier: MS-PL
// REMED-GFX-080: the Software (CPU-raster) backend must honor GraphicsDevice.ScissorRectangle when
// RasterizerState.ScissorTestEnable is true -- for both the 2D SpriteBatch path and the 3D raster
// path -- exactly like the GPU backends corrected by REMED-GFX-013 (Vulkan) / GFX-068 (SdlGpu) etc.
//
// XNA/FNA contract: ScissorRectangle is a framebuffer/target-space rectangle (NOT viewport-local).
// It clips rasterization ONLY when RasterizerState.ScissorTestEnable == true; when false the stored
// rectangle has no effect. With a custom Viewport the effective raster region is
//     framebuffer ∩ Viewport ∩ ScissorRectangle
// and the scissor origin is independent of Viewport.X/Y (no double offset, no viewport-local
// conversion) -- proven against D3D11/Vulkan/SdlGpu, whose scissor is likewise framebuffer-space.
//
// Pre-fix Software signature (this test FAILS pre-fix): SoftwareGraphicsBackend::SetScissorRect was
// a no-op (no rectangle stored) and ApplyRasterizerState discarded its ScissorTestEnable argument,
// so ScissorRectangle never clipped anything -- the whole target was always writable.
//
// Testing strategy (identical to GFX-073/079): read the whole 96x72 backbuffer (or the bound RT)
// once per check via GetBackBufferData (served from the real CPU framebuffer), then bounding-box /
// probe on the CPU. Only binary/full-intensity colors are used for spatial assertions.
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

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Redish(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool Greenish(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool Bluish(const Color& c)   { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }
    bool AnyLit(const Color& c)   { return c.getRProperty() >= 200 || c.getGProperty() >= 200 || c.getBProperty() >= 200; }

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

class SoftwareScissorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
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

    // REMED-GFX-080: a game enables scissor testing through RasterizerState.ScissorTestEnable, set
    // on the device (SpriteBatch.Begin's rasterizerState argument is a separate, currently-unwired
    // path -- see the new-finding note in REMEDIATION_PROGRESS). CullNone so the raster geometry is
    // winding-agnostic and the check isolates scissor clipping.
    void SetScissorEnabled(GraphicsDevice& dev, bool enable)
    {
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setScissorTestEnableProperty(enable);
        dev.setRasterizerStateProperty(rs);
    }

    void SetScissorRect(GraphicsDevice& dev, int x, int y, int w, int h)
    {
        dev.setScissorRectangleProperty(Rectangle(x, y, w, h));
    }

    void DrawSprite(GraphicsDevice& dev, const Rectangle& destination, const Color& tint,
                    float rotation = 0.0f, const Vector2& origin = Vector2(0.0f, 0.0f),
                    SpriteEffects effects = SpriteEffects::None)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint, rotation, origin, effects, 0.0f);
        sb.End();
    }

    // A solid-color NDC quad (identity WVP -> NDC == raw position). CullNone-safe.
    void DrawQuad(GraphicsDevice& dev, float ndcMinX, float ndcMaxX, float ndcMinY, float ndcMaxY,
                  float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(ndcMinX, ndcMaxY, z), color },
            { Vector3(ndcMaxX, ndcMaxY, z), color },
            { Vector3(ndcMaxX, ndcMinY, z), color },
            { Vector3(ndcMinX, ndcMaxY, z), color },
            { Vector3(ndcMaxX, ndcMinY, z), color },
            { Vector3(ndcMinX, ndcMinY, z), color },
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

    // Same full-NDC quad via the indexed path -- proves indexed and non-indexed 3D entry points
    // share the scissor clip.
    void DrawQuadIndexed(GraphicsDevice& dev, float ndcMinX, float ndcMaxX, float ndcMinY,
                         float ndcMaxY, float z, const Color& color)
    {
        const VertexPositionColor verts[4] = {
            { Vector3(ndcMinX, ndcMaxY, z), color },
            { Vector3(ndcMaxX, ndcMaxY, z), color },
            { Vector3(ndcMaxX, ndcMinY, z), color },
            { Vector3(ndcMinX, ndcMinY, z), color },
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

    // A textured full-NDC quad via DrawPrimitivesEx (BasicEffect with a texture) -- proves the
    // shaded/textured 3D path shares the same scissor clip.
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

    // Count lit pixels of `pred` that fall OUTSIDE the inclusive rectangle [x0,x1] x [y0,y1].
    template <typename Pred>
    static int OutsideRect(const std::vector<Color>& pix, Pred pred, int x0, int y0, int x1, int y1)
    {
        int n = 0;
        for (int y = 0; y < kBBH; ++y)
            for (int x = 0; x < kBBW; ++x)
                if (pred(At(pix, x, y)) && (x < x0 || x > x1 || y < y0 || y > y1))
                    ++n;
        return n;
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

        // ================= 2D / SpriteBatch =================

        // ---- A (Phase 5): full viewport, small scissor ENABLED, full-screen red sprite ----------
        // Scissor(21,13,31,19) -> inclusive x[21,51] y[13,31] (half-open x[21,52) y[13,32)).
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 21, 13, 31, 19);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);   // covers whole framebuffer
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 21, 0) && CloseTo(b.maxX, 51, 0) &&
                  CloseTo(b.minY, 13, 0) && CloseTo(b.maxY, 31, 0),
                  "A1: scissor-clipped red == x[21,51] y[13,31]: " + b.str());
            check(OutsideRect(pix, Redish, 21, 13, 51, 31) == 0,
                  "A2: nothing red outside the scissor rectangle");
            check(Redish(At(pix, 21, 13)) && Redish(At(pix, 51, 31)),
                  "A3: scissor corners (21,13) and (51,31) are red");
            check(!Redish(At(pix, 20, 20)) && !Redish(At(pix, 52, 20)) &&
                  !Redish(At(pix, 35, 12)) && !Redish(At(pix, 35, 32)),
                  "A4: just-outside all four sides (20,52 / 12,32) are black");
        }

        // ---- B (Phase 6): SAME small scissor but ScissorTestEnable=false -> whole fb red --------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, false);
            SetScissorRect(dev, 21, 13, 31, 19);   // same small rect, must have NO effect
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(b.count == kBBW * kBBH && b.width() == kBBW && b.height() == kBBH,
                  "B1: scissor DISABLED -> entire 96x72 framebuffer red (rect ignored): " + b.str());
        }

        // ---- C (Phase 7): Viewport x Scissor intersection (scissor is framebuffer-space) --------
        // Viewport(19,11,41,29) clip x[19,59] y[11,39]; Scissor(30,6,25,23) clip x[30,54] y[6,28].
        // Intersection x[30,54] y[11,28]. A viewport-local misread would put the left edge at
        // x=19+30=49 -- ruled out by red at x in [30,48].
        {
            SetVp(dev, 19, 11, 41, 29);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 30, 6, 25, 23);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, 41, 29), red);   // covers the whole viewport (local)
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 30, 0) && CloseTo(b.maxX, 54, 0) &&
                  CloseTo(b.minY, 11, 0) && CloseTo(b.maxY, 28, 0),
                  "C1: framebuffer ∩ Viewport ∩ Scissor == x[30,54] y[11,28]: " + b.str());
            check(Redish(At(pix, 30, 20)) && Redish(At(pix, 40, 20)) && Redish(At(pix, 48, 20)),
                  "C2: scissor is framebuffer-space, not viewport-local (red at x=30..48)");
            check(OutsideRect(pix, Redish, 30, 11, 54, 28) == 0,
                  "C3: nothing red outside the viewport∩scissor rectangle");
        }

        // ---- D (Phase 12): four-side clipping, full viewport ------------------------------------
        // Scissor(25,18,40,30) -> x[25,64] y[18,47]; sprite overflows on every side.
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 25, 18, 40, 30);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(10, 8, 76, 56), red);   // x[10,85] y[8,63], overflows all sides
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 25, 0) && CloseTo(b.maxX, 64, 0) &&
                  CloseTo(b.minY, 18, 0) && CloseTo(b.maxY, 47, 0),
                  "D1: overflowing sprite clipped to exactly x[25,64] y[18,47]: " + b.str());
            check(Redish(At(pix, 25, 30)) && !Redish(At(pix, 24, 30)), "D2: left edge cut at x=25");
            check(Redish(At(pix, 64, 30)) && !Redish(At(pix, 65, 30)), "D3: right edge cut at x=64");
            check(Redish(At(pix, 40, 18)) && !Redish(At(pix, 40, 17)), "D4: top edge cut at y=18");
            check(Redish(At(pix, 40, 47)) && !Redish(At(pix, 40, 48)), "D5: bottom edge cut at y=47");
        }

        // ---- E (Phase 13): scissor extending outside the framebuffer is intersected safely ------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, -10, 5, 30, 20);   // left edge off-screen -> x[0,19] y[5,24]
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 0, 0) && CloseTo(b.maxX, 19, 0) &&
                  CloseTo(b.minY, 5, 0) && CloseTo(b.maxY, 24, 0),
                  "E1: negative-origin scissor clamped to x[0,19] y[5,24]: " + b.str());

            SetScissorRect(dev, 80, 60, 30, 30);   // extends past right/bottom -> x[80,95] y[60,71]
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            const BBox b2 = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b2.minX, 80, 0) && CloseTo(b2.maxX, 95, 0) &&
                  CloseTo(b2.minY, 60, 0) && CloseTo(b2.maxY, 71, 0),
                  "E2: over-right/bottom scissor clamped to x[80,95] y[60,71] (no OOB): " + b2.str());
        }

        // ---- F (Phase 14): scissor entirely outside framebuffer / viewport -> zero pixels -------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 120, 80, 10, 10);   // fully outside the 96x72 framebuffer
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            check(Box(ReadBackbuffer(dev), Redish).count == 0,
                  "F1: scissor fully outside framebuffer -> zero pixels drawn");

            SetVp(dev, 0, 0, 40, 40);
            SetScissorRect(dev, 60, 60, 10, 10);    // outside the (0,0,40,40) viewport
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, 40, 40), red);
            SetVp(dev, 0, 0, kBBW, kBBH);
            check(Box(ReadBackbuffer(dev), Redish).count == 0,
                  "F2: scissor outside the active viewport -> zero pixels drawn");
        }

        // ---- G (Phase 15): zero-sized scissor -> empty clip, no writes, no UB -------------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            for (const Rectangle r : { Rectangle(30, 20, 0, 0), Rectangle(30, 20, 0, 15),
                                       Rectangle(30, 20, 15, 0) })
            {
                dev.setScissorRectangleProperty(r);
                dev.Clear(Color::Black);
                DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
                check(Box(ReadBackbuffer(dev), Redish).count == 0,
                      "G: zero-sized scissor (" + std::to_string(r.Width) + "x" +
                      std::to_string(r.Height) + ") -> nothing drawn");
            }
        }

        // ---- H (Phase 19): multiple scissors in one frame, each draw clips independently --------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            dev.Clear(Color::Black);
            SetScissorRect(dev, 10, 10, 20, 20);   // x[10,29] y[10,29]
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            SetScissorRect(dev, 50, 40, 25, 20);   // x[50,74] y[40,59]
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), green);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox rb = Box(pix, Redish);
            const BBox gb = Box(pix, Greenish);
            check(CloseTo(rb.minX, 10, 0) && CloseTo(rb.maxX, 29, 0) &&
                  CloseTo(rb.minY, 10, 0) && CloseTo(rb.maxY, 29, 0),
                  "H1: draw A clipped to scissor A x[10,29] y[10,29]: " + rb.str());
            check(CloseTo(gb.minX, 50, 0) && CloseTo(gb.maxX, 74, 0) &&
                  CloseTo(gb.minY, 40, 0) && CloseTo(gb.maxY, 59, 0),
                  "H2: draw B clipped to scissor B x[50,74] y[40,59]: " + gb.str());
        }

        // ---- I (Phase 20): rectangle and enable are independent state ---------------------------
        // (1) rect set + enabled -> clipped;  (2) SAME rect, disabled -> not clipped;
        // (3) enabled again WITHOUT re-setting the rect -> stored rect becomes active again.
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorRect(dev, 20, 15, 25, 20);   // x[20,44] y[15,34]

            SetScissorEnabled(dev, true);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            const BBox a = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(a.minX, 20, 0) && CloseTo(a.maxX, 44, 0) &&
                  CloseTo(a.minY, 15, 0) && CloseTo(a.maxY, 34, 0),
                  "I1: enabled -> clipped to x[20,44] y[15,34]: " + a.str());

            SetScissorEnabled(dev, false);          // rect unchanged, testing off
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), green);
            const BBox b = Box(ReadBackbuffer(dev), Greenish);
            check(b.count == kBBW * kBBH,
                  "I2: same rect, disabled -> full framebuffer (rect inert): " + b.str());

            SetScissorEnabled(dev, true);           // re-enable WITHOUT re-setting the rectangle
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), blue);
            const BBox c = Box(ReadBackbuffer(dev), Bluish);
            check(CloseTo(c.minX, 20, 0) && CloseTo(c.maxX, 44, 0) &&
                  CloseTo(c.minY, 15, 0) && CloseTo(c.maxY, 34, 0),
                  "I3: re-enabled (rect not re-set) -> stored rect active again x[20,44] y[15,34]: " + c.str());
        }

        // ---- J (Phase 22/23): scissor is NOT transformed by transformMatrix / flip -------------
        // Custom viewport + custom scissor + non-identity transformMatrix + FlipH/FlipV. The scissor
        // clips final raster pixels in FRAMEBUFFER space, so a sprite that (after the transform)
        // overflows the scissor on every side is clipped to exactly the fb-space viewport∩scissor
        // rect -- unmoved by the 1.3x scale / (2,3) translate, unaffected by the UV flips.
        {
            SetVp(dev, 19, 11, 41, 29);            // clip x[19,59] y[11,39]
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 30, 15, 20, 15);   // fb-space x[30,49] y[15,29]; ∩ vp = x[30,49] y[15,29]
            dev.Clear(Color::Black);
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                Matrix transform = Matrix::getIdentityProperty();
                transform.M11 = 1.3f; transform.M22 = 1.3f;
                transform.M41 = 2.0f; transform.M42 = 3.0f;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
                         nullptr, transform);
                sb.Draw(whiteTex_, Rectangle(0, 0, 50, 40), Rectangle(0, 0, 1, 1), red,
                        0.0f, Vector2(0.0f, 0.0f),
                        SpriteEffects(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                      static_cast<int>(SpriteEffects::FlipVertically)),
                        0.0f);
                sb.End();
            }
            SetVp(dev, 0, 0, kBBW, kBBH);
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 30, 0) && CloseTo(b.maxX, 49, 0) &&
                  CloseTo(b.minY, 15, 0) && CloseTo(b.maxY, 29, 0),
                  "J1: transformed+flipped sprite clipped to exactly fb-space scissor∩viewport x[30,49] y[15,29]: " + b.str());
            check(OutsideRect(pix, Redish, 30, 15, 49, 29) == 0,
                  "J2: scissor not scaled/translated by transformMatrix (nothing red outside)");
        }

        // ---- J' (Phase 23): rotation -- scissor clips final rasterized pixels only --------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 30, 20, 25, 20);   // x[30,54] y[20,39]
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(20, 10, 50, 45), red, 0.4f, Vector2(0.0f, 0.0f));  // rotated, overlaps scissor
            const std::vector<Color> pix = ReadBackbuffer(dev);
            check(OutsideRect(pix, Redish, 30, 20, 54, 39) == 0,
                  "J'1: rotated sprite's pixels never escape the scissor rectangle");
            check(Box(pix, Redish).count > 0, "J'2: rotated sprite rendered some red inside the scissor");
        }

        // ================= 3D raster path =================

        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // ---- K (Phase 10/11): 3D DrawPrimitives, SAME scissor as A -> identical clip ------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 21, 13, 31, 19);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);   // full-NDC quad = whole framebuffer
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Redish);
            check(CloseTo(b.minX, 21, 0) && CloseTo(b.maxX, 51, 0) &&
                  CloseTo(b.minY, 13, 0) && CloseTo(b.maxY, 31, 0),
                  "K1: 3D scissor clip == x[21,51] y[13,31] (identical to SpriteBatch A): " + b.str());
            check(OutsideRect(pix, Redish, 21, 13, 51, 31) == 0, "K2: no 3D red outside the scissor");
        }

        // ---- L (Phase 10): 3D DrawIndexedPrimitives -> identical clip ---------------------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 21, 13, 31, 19);
            dev.Clear(Color::Black);
            DrawQuadIndexed(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b.minX, 21, 0) && CloseTo(b.maxX, 51, 0) &&
                  CloseTo(b.minY, 13, 0) && CloseTo(b.maxY, 31, 0),
                  "L1: 3D indexed scissor clip == x[21,51] y[13,31] (== non-indexed): " + b.str());
        }

        // ---- M (Phase 7 for 3D): Viewport x Scissor intersection, SAME as SpriteBatch C ---------
        {
            SetVp(dev, 19, 11, 41, 29);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 30, 6, 25, 23);
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);   // fills the whole viewport
            SetVp(dev, 0, 0, kBBW, kBBH);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b.minX, 30, 0) && CloseTo(b.maxX, 54, 0) &&
                  CloseTo(b.minY, 11, 0) && CloseTo(b.maxY, 28, 0),
                  "M1: 3D fb ∩ Viewport ∩ Scissor == x[30,54] y[11,28] (== SpriteBatch C): " + b.str());
        }

        // ---- N (Phase 28): textured 3D path (DrawPrimitivesEx) shares the scissor clip ----------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 21, 13, 31, 19);
            dev.Clear(Color::Black);
            DrawTexturedQuad(dev, greenTex_);   // green texture over the whole framebuffer
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox b = Box(pix, Greenish);
            check(CloseTo(b.minX, 21, 0) && CloseTo(b.maxX, 51, 0) &&
                  CloseTo(b.minY, 13, 0) && CloseTo(b.maxY, 31, 0),
                  "N1: textured DrawPrimitivesEx scissor clip == x[21,51] y[13,31]: " + b.str());
            check(OutsideRect(pix, Greenish, 21, 13, 51, 31) == 0, "N2: no textured green outside the scissor");
        }

        // ---- O (Phase 25): 3D depth control under an enabled scissor ----------------------------
        // Depth affects only which color wins; scissor affects only XY. near-red beats far-blue
        // inside the scissor, and both are clipped to the scissor rect.
        {
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 20, 15, 30, 25);   // x[20,49] y[15,39]
            dev.Clear(Color::Black);   // also clears depth to 1.0
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.2f, red);    // near
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.8f, blue);   // far (loses the depth test)
            const std::vector<Color> pix = ReadBackbuffer(dev);
            const BBox rb = Box(pix, Redish);
            check(CloseTo(rb.minX, 20, 0) && CloseTo(rb.maxX, 49, 0) &&
                  CloseTo(rb.minY, 15, 0) && CloseTo(rb.maxY, 39, 0),
                  "O1: near-red wins inside scissor, clipped to x[20,49] y[15,39]: " + rb.str());
            check(Box(pix, Bluish).count == 0, "O2: far-blue lost the depth test everywhere (no blue)");
            check(OutsideRect(pix, AnyLit, 20, 15, 49, 39) == 0, "O3: nothing lit outside the scissor");
            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        // ---- P (Phase 6 for 3D): scissor DISABLED -> full framebuffer (3D control) --------------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, false);
            SetScissorRect(dev, 21, 13, 31, 19);   // small rect must be ignored
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(b.count == kBBW * kBBH,
                  "P1: 3D scissor DISABLED -> entire framebuffer red (rect ignored): " + b.str());
        }

        // ---- Q (Phase 33 UB-guard for 3D): negative / zero-size scissor on the 3D path ----------
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, -8, -4, 40, 30);   // x[0,31] y[0,25]
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            const BBox b = Box(ReadBackbuffer(dev), Redish);
            check(CloseTo(b.minX, 0, 0) && CloseTo(b.maxX, 31, 0) &&
                  CloseTo(b.minY, 0, 0) && CloseTo(b.maxY, 25, 0),
                  "Q1: 3D negative-origin scissor clamped to x[0,31] y[0,25] (no OOB): " + b.str());

            SetScissorRect(dev, 40, 30, 0, 0);     // zero-size on the 3D path
            dev.Clear(Color::Black);
            DrawQuad(dev, -1.0f, 1.0f, -1.0f, 1.0f, 0.5f, red);
            check(Box(ReadBackbuffer(dev), Redish).count == 0, "Q2: 3D zero-size scissor -> nothing drawn");
        }

        // ================= RenderTarget2D =================

        // ---- R (Phase 17): scissor is relative to the bound RenderTarget2D's framebuffer --------
        {
            const int rtW = 48, rtH = 40;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 30, 20, 40, 30);   // x[30,47] y[20,39] on the 48x40 RT (clamped by RT)
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, rtW, rtH), red);   // covers the whole RT

            std::vector<Color> pix(static_cast<std::size_t>(rtW) * rtH, Color(0, 0, 0, 0));
            const Rectangle whole(0, 0, rtW, rtH);
            dev.GetBackBufferData(&whole, pix.data(), 0, static_cast<int>(pix.size()));

            int minX = rtW, minY = rtH, maxX = -1, maxY = -1, count = 0, oob = 0;
            for (int y = 0; y < rtH; ++y)
                for (int x = 0; x < rtW; ++x)
                    if (Redish(pix[static_cast<std::size_t>(y) * rtW + x]))
                    {
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                        ++count;
                        if (x < 30 || x > 47 || y < 20 || y > 39) ++oob;
                    }
            const std::string s = "x[" + std::to_string(minX) + "," + std::to_string(maxX) + "] y["
                + std::to_string(minY) + "," + std::to_string(maxY) + "] n=" + std::to_string(count);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);
            check(count > 0 && CloseTo(minX, 30, 0) && CloseTo(maxX, 47, 0) &&
                  CloseTo(minY, 20, 0) && CloseTo(maxY, 39, 0) && oob == 0,
                  "R1: RT-bound scissor clipped in RT space to x[30,47] y[20,39]: " + s);
        }

        // ---- S (Phase 18): no stale RT scissor leaks back to the backbuffer ---------------------
        // backbuffer(small scissor) -> RT(own scissor) -> backbuffer: the RT transition resets the
        // scissor to the full target, so after unbinding, a full-screen draw is NOT clipped to the
        // RT's (or the earlier backbuffer's) small rectangle.
        {
            SetVp(dev, 0, 0, kBBW, kBBH);
            SetScissorEnabled(dev, true);
            SetScissorRect(dev, 5, 5, 15, 15);     // backbuffer small scissor
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), red);
            check(Box(ReadBackbuffer(dev), Redish).count == 15 * 15,
                  "S1: backbuffer small scissor clips (15x15) before the RT transition");

            const int rtW = 48, rtH = 40;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);   // resets viewport+scissor to full RT (0,0,48,40)
            SetScissorRect(dev, 8, 6, 12, 10);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, rtW, rtH), green);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));   // resets scissor to full backbuffer

            // Scissor testing is still ENABLED (RasterizerState unchanged), but the rect was reset
            // to the full backbuffer by the transition -> a full-screen draw covers the whole fb.
            SetVp(dev, 0, 0, kBBW, kBBH);
            dev.Clear(Color::Black);
            DrawSprite(dev, Rectangle(0, 0, kBBW, kBBH), blue);
            const BBox b = Box(ReadBackbuffer(dev), Bluish);
            check(b.count == kBBW * kBBH,
                  "S2: after RT unbind the scissor reset to full backbuffer (no stale RT rect): " + b.str());
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareScissorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareScissorTest game;
    game.Run();
    return game.getResult();
}
