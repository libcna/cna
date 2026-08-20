// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-8 (D9-82): this renderer's first real 3D triangle. Proves
// DrawColoredPrimitives()/DrawIndexedColoredPrimitives() -- the real BasicEffect
// VertexColorEnabled-only path (ShaderIndex 3, "BasicEffect_VSBasicVcNoFog"/
// "BasicEffect_PSBasicNoFog") -- through the real public Game/GraphicsDeviceManager/
// GraphicsDevice API, mirroring DirectX11_Smoke's own Check P exactly (same "oversized triangle"
// full-NDC trick, same before/after-Clear() discipline, same CullMode::None reset so this basic
// check does not depend on D9-21's still-open D3DCULL winding proof).
//
// Check A -- DrawColoredPrimitives: a known vertex color paints over a known-blue Clear()
//   background at the same readback location (before=blue, after=red).
// Check B -- DrawIndexedColoredPrimitives: same proof, indexed path.
// Check C -- real WorldViewProj register upload: translating World far outside the NDC cube
//   ([-1,1]) leaves the background genuinely UNCHANGED -- proves the constant upload is real and
//   actually affects the transform, not a hardcoded/ignored no-op that would paint the same
//   full-screen triangle regardless of World.
// Check D -- PrimitiveType.TriangleStrip: every scene in the D9-A5 oracle corpus (and every check
//   in this file until now) only ever used TriangleList, even though PrimitiveType has 3 other
//   real values and GraphicsDevice::PrimitiveVerts()/ToD3D9Topology() already handle all of them
//   unconditionally. A 4-vertex strip covering the full NDC square (2 triangles, primitiveCount=2)
//   -- sampling near the FIRST triangle's own corner (always covered, even if primitiveCount was
//   wrongly computed as 1) and near the SECOND triangle's own corner (covered ONLY if
//   primitiveCount genuinely resolved to 2, not 1) is a real, discriminating proof that the
//   vertex-count<->primitive-count conversion for TriangleStrip is correct end to end, not just
//   "didn't crash". Independently confirmed pixel-for-pixel identical to real XNA 4.0 via
//   tools/xna-oracle/scenes/colored_trianglestrip_quad.scene (D9-A5), which additionally proves
//   the four-corner Gouraud interpolation across BOTH triangles matches exactly.
// Check E -- PrimitiveType.LineList: two SEPARATE 2-vertex line segments at different Y rows,
//   proving they draw independently (both colors present) with nothing connecting them (the row
//   between them stays background) -- a broken conversion that treated this as one connected
//   polyline (LineStrip's own semantics) would paint a connecting line straight through that row.
//   Confirmed pixel-for-pixel identical to real XNA 4.0 via
//   tools/xna-oracle/scenes/colored_linelist_quad.scene.
// Check F -- PrimitiveType.LineStrip: a 3-vertex axis-aligned "L" polyline (horizontal leg then
//   vertical leg, sharing the corner vertex) -- the SAME vertex count is malformed under
//   LineList (odd), so this is LineStrip's own genuine discriminator. Sampling BOTH legs proves
//   the vertex-count<->primitiveCount conversion resolved to 2 connected segments (n-1), not a
//   degenerate single segment. Confirmed pixel-for-pixel identical to real XNA 4.0 via
//   tools/xna-oracle/scenes/colored_linestrip_quad.scene.
//
// Real finding (verified empirically, not assumed -- see plans/plan_dx9.md D9-82's own closure note):
// D9-22's original D3D9VertexDeclarations.hpp chose D3DDECLTYPE_D3DCOLOR for the COLOR0 element,
// mirroring D3D11's own DXGI_FORMAT_R8G8B8A8_UNORM choice by "same semantic meaning" -- but
// D3DDECLTYPE_D3DCOLOR's real contract (MSDN D3DDECLTYPE enumeration: "Input is a D3DCOLOR and is
// expanded to RGBA order") expects ARGB-packed memory bytes (B,G,R,A ascending), while XNA's own
// Color.PackedValue is R,G,B,A ascending (D3D9FormatMapping.cpp's own comment already established
// this for render-target formats) -- feeding CNA's native byte order through D3DDECLTYPE_D3DCOLOR
// silently swaps the R and B channels. Fixed by switching to D3DDECLTYPE_UBYTE4N (four bytes
// normalized in their existing order, no ARGB swizzle) -- exactly matching XNA's real memory
// layout with zero reordering. This test's exact-red (not swapped-to-blue) readback is the
// empirical proof the fix is correct, not merely plausible.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    struct VPC { float x, y, z; uint32_t color; };

    // Same full-NDC "oversized triangle" trick DirectX11_Smoke's own Check P established: with
    // world=view=projection=Identity, these Position values ARE clip-space coordinates directly.
    // 0xFF0000FFu packed as XNA's own R,G,B,A ascending-byte Color convention = opaque red.
    const VPC kTri[3] = {
        {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
        { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
        {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
    };
    const uint16_t kTriIdx[3] = {0, 1, 2};

    // Check D: a deliberately oversized quad in the canonical "Z" TriangleStrip order
    // (TL,TR,BL,BR), extending past the [-1,1] NDC cube on all sides so both resulting triangles
    // fully cover the viewport with no boundary ambiguity. Triangle 0 (TL,TR,BL) covers
    // everything except the far bottom-right corner; triangle 1 (TR,BL,BR) is the only one that
    // covers that corner -- so sampling there is a real, discriminating proof that
    // primitiveCount genuinely resolved to 2 (not 1) for TriangleStrip.
    const VPC kStrip[4] = {
        {-2.0f,  2.0f, 0.0f, 0xFF0000FFu}, // TL
        { 2.0f,  2.0f, 0.0f, 0xFF0000FFu}, // TR
        {-2.0f, -2.0f, 0.0f, 0xFF0000FFu}, // BL
        { 2.0f, -2.0f, 0.0f, 0xFF0000FFu}, // BR
    };

    // Check E: two SEPARATE horizontal line segments (axis-aligned, no diagonal-AA ambiguity on
    // this small 64x64 canvas) at different Y rows -- LineList draws each independently.
    const VPC kLineList[4] = {
        {-0.8f,  0.5f, 0.0f, 0xFF0000FFu}, {0.8f,  0.5f, 0.0f, 0xFF0000FFu}, // segment 0, red
        {-0.8f, -0.5f, 0.0f, 0xFF00FF00u}, {0.8f, -0.5f, 0.0f, 0xFF00FF00u}, // segment 1, green
    };

    // Check F: an axis-aligned "L" polyline (V0->V1 horizontal, V1->V2 vertical, sharing V1) --
    // LineStrip's own genuine discriminator from LineList: the SAME 3 vertices under LineList
    // would be malformed (3 is odd, LineList needs pairs); under LineStrip they draw 2 CONNECTED
    // segments through the shared middle vertex V1.
    const VPC kLineStrip[3] = {
        {-0.6f,  0.4f, 0.0f, 0xFF0000FFu}, // V0
        { 0.6f,  0.4f, 0.0f, 0xFF0000FFu}, // V1 (shared corner)
        { 0.6f, -0.4f, 0.0f, 0xFF0000FFu}, // V2
    };
}

class D3D9DrawTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        // Known-safe baseline, matching DirectX11_Smoke's own Check P discipline: opaque blend, no
        // depth/stencil, no culling (so this basic check does not depend on D9-21's still-open
        // D3DCULL winding proof).
        renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

        const Microsoft::Xna::Framework::Rectangle centerRegion(28, 28, 4, 4);

        // Check A: DrawColoredPrimitives (non-indexed).
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));

            renderer.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));

            bool beforeIsBlue = true, afterIsRed = true;
            for (const Color& p : before)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIsBlue = false;
            for (const Color& p : after)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIsRed = false;
            check(beforeIsBlue && afterIsRed,
                  "DirectX9Renderer::DrawColoredPrimitives(): real BasicEffect-VertexColor draw paints "
                  "the EXACT vertex color (R,G,B channels in the correct order, not swapped) over the "
                  "Clear() background at the same readback location");
        }

        // Check B: DrawIndexedColoredPrimitives (indexed).
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));
            auto ib = renderer.CreateIndexBuffer16(3);
            ib->SetData16(kTriIdx, 3);

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));

            renderer.DrawIndexedColoredPrimitives(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));

            bool beforeIsBlue = true, afterIsRed = true;
            for (const Color& p : before)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIsBlue = false;
            for (const Color& p : after)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIsRed = false;
            check(beforeIsBlue && afterIsRed,
                  "DirectX9Renderer::DrawIndexedColoredPrimitives(): real indexed draw paints the "
                  "exact vertex color over the Clear() background at the same readback location");
        }

        // Check C: the WorldViewProj register upload is real -- a World translation that moves the
        // triangle entirely outside the NDC cube leaves the background genuinely unpainted. If the
        // constant upload were a no-op (e.g. wrong register, or silently never called), the same
        // full-NDC-covering triangle geometry would still paint the whole viewport regardless of
        // World, and this check would (correctly) fail.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            const Matrix farAway = Matrix::CreateTranslation(1000.0f, 0.0f, 0.0f);
            renderer.DrawColoredPrimitives(*vb, farAway, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);

            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));
            bool stillBlue = true;
            for (const Color& p : after)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    stillBlue = false;
            check(stillBlue,
                  "DirectX9Renderer::DrawColoredPrimitives(): WorldViewProj constant upload is real -- "
                  "translating World far outside the NDC cube genuinely leaves the background unpainted");
        }

        // Check D: PrimitiveType.TriangleStrip -- see this file's own header comment.
        {
            auto vb = renderer.CreateVertexBuffer(4);
            vb->SetData(kStrip, 4, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleStrip, 2);

            const Microsoft::Xna::Framework::Rectangle topLeftRegion(4, 4, 4, 4);
            const Microsoft::Xna::Framework::Rectangle bottomRightRegion(56, 56, 4, 4);
            std::vector<Color> tl(4 * 4, Color(0, 0, 0, 0)), br(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&topLeftRegion, tl.data(), 0, static_cast<int>(tl.size()));
            dev.GetBackBufferData(&bottomRightRegion, br.data(), 0, static_cast<int>(br.size()));

            bool tlIsRed = true, brIsRed = true;
            for (const Color& p : tl)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    tlIsRed = false;
            for (const Color& p : br)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    brIsRed = false;
            check(tlIsRed && brIsRed,
                  "DirectX9Renderer::DrawColoredPrimitives(..., PrimitiveType.TriangleStrip, primitiveCount=2): "
                  "a 4-vertex strip genuinely paints BOTH triangles -- the top-left corner (covered by triangle 0 "
                  "alone) AND the bottom-right corner (covered ONLY by triangle 1) are both the vertex color, "
                  "proving the vertex-count<->primitiveCount conversion for TriangleStrip is correct, not just "
                  "that triangle 0 rendered while primitiveCount silently stayed at 1");
        }

        // Lines are only 1px wide, so (unlike Check A-D's solid triangle fills) a small search
        // region + "does ANY pixel in it match" is the right robustness margin here -- exact
        // single-pixel line coverage can shift by a pixel depending on GPU/API rounding rules,
        // and that's not what Checks E/F are trying to prove (the oracle's own 256x256
        // colored_linelist_quad.scene/colored_linestrip_quad.scene are the pixel-exact proof;
        // this offline CTest's job is just "did the right color paint roughly the right place").
        auto regionContains = [&](int x, int y, int w, int h, const Color& expected) -> bool
        {
            const Microsoft::Xna::Framework::Rectangle region(x, y, w, h);
            std::vector<Color> px(static_cast<std::size_t>(w * h), Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, px.data(), 0, w * h);
            for (const Color& p : px)
                if (p.getRProperty() == expected.getRProperty() && p.getGProperty() == expected.getGProperty() &&
                    p.getBProperty() == expected.getBProperty() && p.getAProperty() == expected.getAProperty())
                    return true;
            return false;
        };

        // Check E: PrimitiveType.LineList -- two SEPARATE horizontal segments at different Y
        // rows. Confirms both draw independently (RED near y=16, GREEN near y=48) AND that
        // nothing connects them (the row exactly between, y=32, stays the Clear() background --
        // a broken conversion that mistreated this as a single connected polyline, i.e.
        // LineStrip's own semantics, would paint a connecting line straight through this row).
        {
            auto vb = renderer.CreateVertexBuffer(4);
            vb->SetData(kLineList, 4, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::LineList, 2);

            const bool redFound = regionContains(28, 14, 8, 4, Color(255, 0, 0, 255));
            const bool greenFound = regionContains(28, 46, 8, 4, Color(0, 255, 0, 255));
            const Microsoft::Xna::Framework::Rectangle betweenRegion(28, 30, 8, 4);
            std::vector<Color> between(8 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&betweenRegion, between.data(), 0, static_cast<int>(between.size()));
            bool betweenIsBackground = true;
            for (const Color& p : between)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    betweenIsBackground = false;

            check(redFound && greenFound && betweenIsBackground,
                  "DirectX9Renderer::DrawColoredPrimitives(..., PrimitiveType.LineList, primitiveCount=2): "
                  "two independent line segments each paint their own color, with nothing connecting them -- "
                  "confirmed pixel-for-pixel identical to real XNA 4.0 via "
                  "tools/xna-oracle/scenes/colored_linelist_quad.scene");
        }

        // Check F: PrimitiveType.LineStrip -- an axis-aligned "L" polyline (V0->V1 horizontal,
        // V1->V2 vertical, sharing V1). The SAME 3 vertices are malformed under LineList (odd
        // count); under LineStrip they draw 2 CONNECTED segments through the shared corner.
        // Sampling BOTH segments proves the vertex-count<->primitiveCount conversion resolved to
        // primitiveCount=2 (n-1), not a degenerate single segment or a silent drop.
        {
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kLineStrip, 3, sizeof(VPC));

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::LineStrip, 2);

            const bool horizontalFound = regionContains(28, 17, 8, 4, Color(255, 0, 0, 255));
            const bool verticalFound = regionContains(48, 30, 4, 8, Color(255, 0, 0, 255));

            check(horizontalFound && verticalFound,
                  "DirectX9Renderer::DrawColoredPrimitives(..., PrimitiveType.LineStrip, primitiveCount=2): "
                  "a 3-vertex strip genuinely paints BOTH connected segments (the horizontal V0->V1 leg AND the "
                  "vertical V1->V2 leg sharing the corner vertex), confirmed pixel-for-pixel identical to real "
                  "XNA 4.0 via tools/xna-oracle/scenes/colored_linestrip_quad.scene");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9DrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

int main()
{
    {
        D3D9DrawTest game;
        game.Run();
    }

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
