// SPDX-License-Identifier: MS-PL
// REMED-GFX-083: the Software (CPU-raster) renderer must honor RasterizerState.DepthBias and
// RasterizerState.SlopeScaleDepthBias. Before this task SoftwareRenderer::ApplyRasterizerState
// dropped BOTH float arguments (its 4th/5th params were unnamed `float, float`), so the per-fragment
// depth written by the rasterizer received no polygon offset -- Software behaved as if DepthBias == 0
// and SlopeScaleDepthBias == 0 for every draw. The GPU renderers honor these: EasyGL/Vulkan feed them
// UNSCALED into glPolygonOffset(slopeScaleDepthBias, depthBias) / vkCmdSetDepthBias(depthBias, 0,
// slopeScaleDepthBias), and D3D11 rounds DepthBias to D3D11_RASTERIZER_DESC::DepthBias (INT) and maps
// SlopeScaleDepthBias to ::SlopeScaledDepthBias. So coplanar/decal geometry that relies on a depth
// bias to win/lose a z-fight rendered differently on Software than on the GPU renderers.
//
// XNA/FNA + cross-renderer contract (verified against FNA RasterizerState, GraphicsDevice's unscaled
// forwarding, and CNA's D3D11/Vulkan/EasyGL ApplyRasterizerState):
//   effectiveOffset = SlopeScaleDepthBias * m + DepthBias * r        (added to post-viewport depth)
//     m = max(|dz/dx|, |dz/dy|)  -- the triangle's max screen-space depth slope (window depth, pixels)
//     r = the depth buffer's minimum resolvable difference (Software float32 buffer: 2^-24, XNA's
//         canonical Depth24 unit -- the same magnitude D3D11's 24-bit UNORM path uses)
//   Sign: POSITIVE bias INCREASES depth == pushes the polygon AWAY from the camera (toward far); a
//         later coplanar polygon with positive bias LOSES, with negative bias WINS. Applied in
//         window-depth space (after Viewport.MinDepth/MaxDepth), matching GL/D3D/Vulkan polygon offset.
//
// Test method (identical to GFX-073/079/080/082): render into the 96x72 backbuffer (or a bound RT) with
// depth testing on, using EXACTLY the same coplanar geometry for the two contenders so per-pixel depth
// is bit-identical (no z-fighting noise -- the flip comes only from the bias), read back the real CPU
// framebuffer via GetBackBufferData, and probe the center. Bias magnitudes are large integer-valued
// floats so the offset is an unambiguous ~0.18 in normalized depth (and D3D11's round-to-int preserves
// them, keeping cross-renderer parity).
//
// Pre-fix (bias dropped): every "biased" check below sees the SAME result as its zero-bias baseline
// (the later coplanar polygon always wins), so the biased checks FAIL. Post-fix they flip.
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
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

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
    constexpr int kBBW = 96;
    constexpr int kBBH = 72;

    // A large integer-valued DepthBias: offset = 3e6 * 2^-24 == 0.17881 in normalized depth. Unambiguous
    // (not z-fighting noise) and integer so D3D11's round-to-int keeps the same value (cross-renderer
    // parity). SlopeScaleDepthBias for the slope section: 150 * m (m ~ 0.003 for the sloped quad below)
    // ~ 0.45, a clean full flip.
    constexpr float kBias = 3000000.0f;
    constexpr float kSlopeScale = 150.0f;

    bool isRed(const Color& c)   { return c.getRProperty() >= 200 && c.getGProperty() <= 60 && c.getBProperty() <= 60; }
    bool isGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
    bool isBlue(const Color& c)  { return c.getBProperty() >= 200 && c.getRProperty() <= 60 && c.getGProperty() <= 60; }

    template <typename Pred>
    int CountIf(const std::vector<Color>& pix, Pred pred)
    {
        int n = 0;
        for (const Color& c : pix) if (pred(c)) ++n;
        return n;
    }
}

class SoftwareDepthBiasTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    // Assigns a RasterizerState with the given fill mode + depth bias to the device (CullNone so the
    // quads' winding never matters). The 3D draw paths read GraphicsDevice.RasterizerState directly.
    void SetRaster(GraphicsDevice& dev, FillMode fill, float depthBias, float slopeScaleDepthBias)
    {
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        rs.setFillModeProperty(fill);
        rs.setDepthBiasProperty(depthBias);
        rs.setSlopeScaleDepthBiasProperty(slopeScaleDepthBias);
        dev.setRasterizerStateProperty(rs);
    }

    // ---- geometry: a flat coplanar quad at depth z, filling NDC [-0.7,0.7]^2 (covers center 48,36) ----

    // DrawColoredPrimitives path (RasterizeTriangle).
    void FlatQuadColored(GraphicsDevice& dev, float z, const Color& c)
    {
        const VertexPositionColor v[6] = {
            { Vector3(-0.7f,  0.7f, z), c }, { Vector3( 0.7f,  0.7f, z), c }, { Vector3( 0.7f, -0.7f, z), c },
            { Vector3(-0.7f,  0.7f, z), c }, { Vector3( 0.7f, -0.7f, z), c }, { Vector3(-0.7f, -0.7f, z), c },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    // DrawPrimitivesEx path (RasterizeTriangleShaded) -- a VertexBuffer draw.
    void FlatQuadEx(GraphicsDevice& dev, float z, const Color& c)
    {
        const VertexPositionColor v[6] = {
            { Vector3(-0.7f,  0.7f, z), c }, { Vector3( 0.7f,  0.7f, z), c }, { Vector3( 0.7f, -0.7f, z), c },
            { Vector3(-0.7f,  0.7f, z), c }, { Vector3( 0.7f, -0.7f, z), c }, { Vector3(-0.7f, -0.7f, z), c },
        };
        VertexBuffer vb(dev, 6);
        vb.SetData(v, 6);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // A wider flat quad at depth z, filling NDC [-0.85,0.85]^2 -- the reference the slope test compares
    // against (must cover everything the sloped quad covers).
    void WideFlatQuadColored(GraphicsDevice& dev, float z, const Color& c)
    {
        const VertexPositionColor v[6] = {
            { Vector3(-0.85f,  0.85f, z), c }, { Vector3( 0.85f,  0.85f, z), c }, { Vector3( 0.85f, -0.85f, z), c },
            { Vector3(-0.85f,  0.85f, z), c }, { Vector3( 0.85f, -0.85f, z), c }, { Vector3(-0.85f, -0.85f, z), c },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    // A SLOPED quad: depth is zLeft at NDC x=-0.7 and zRight at NDC x=+0.7 (linear in x, constant in y),
    // so it has a real, uniform screen-space depth slope -- the slope-scale-bias contributor.
    void SlopedQuadColored(GraphicsDevice& dev, float zLeft, float zRight, const Color& c)
    {
        const VertexPositionColor v[6] = {
            { Vector3(-0.7f,  0.7f, zLeft),  c }, { Vector3( 0.7f,  0.7f, zRight), c }, { Vector3( 0.7f, -0.7f, zRight), c },
            { Vector3(-0.7f,  0.7f, zLeft),  c }, { Vector3( 0.7f, -0.7f, zRight), c }, { Vector3(-0.7f, -0.7f, zLeft),  c },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    // A single coplanar triangle at depth z (for the wireframe section) filling NDC roughly the center.
    void TriColored(GraphicsDevice& dev, float z, const Color& c)
    {
        const VertexPositionColor v[3] = {
            { Vector3(-0.7f, -0.7f, z), c }, { Vector3(0.7f, -0.7f, z), c }, { Vector3(0.0f, 0.7f, z), c },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 1);
    }

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

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

#if defined(CNA_RENDERER_LLGL)
        bool rejected = false;
        try
        {
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
        }
        catch (const System::NotSupportedException&)
        {
            rejected = true;
        }
        check(rejected,
              "LLGL: non-zero RasterizerState.DepthBias is rejected deterministically on the "
              "validated OpenGL path");
        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
        return;
#endif

        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);
        const Color blue(0, 0, 255, 255);

        dev.setDepthStencilStateProperty(DepthStencilState::Default);   // depth test + write on
        SetVp(dev, 0, 0, kBBW, kBBH);

        // ===== A (Phase 5/6): CONSTANT DepthBias, colored path -- POSITIVE bias makes the later =======
        //       coplanar polygon LOSE. Baseline (bias 0) -> green; positive bias -> red.
        {
            // A0 baseline: red then green, both bias 0 -> the later coplanar (green) wins.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            FlatQuadColored(dev, 0.5f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "A0: baseline coplanar (both DepthBias=0) -> later polygon (green) wins");

            // A1 biased: red (bias 0) then green with POSITIVE bias -> green pushed back, LOSES -> red.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
            FlatQuadColored(dev, 0.5f, green);
            check(isRed(At(Read(dev), 48, 36)),
                  "A1: POSITIVE DepthBias on the later coplanar (green) pushes it back -> it LOSES (red wins)"
                  " [FAILS pre-fix: bias dropped -> green]");
        }

        // ===== B (Phase 5): SIGN -- NEGATIVE DepthBias makes an EARLIER coplanar polygon WIN over a ====
        //       later one drawn at equal depth. Baseline -> red; negative bias on the first -> green.
        {
            // B0 baseline: green then red, both bias 0 -> later (red) wins.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, green);
            FlatQuadColored(dev, 0.5f, red);
            check(isRed(At(Read(dev), 48, 36)),
                  "B0: baseline coplanar (both DepthBias=0) -> later polygon (red) wins");

            // B1 biased: green with NEGATIVE bias first (pulled forward), then red bias 0 -> red LOSES.
            SetRaster(dev, FillMode::Solid, -kBias, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, green);
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            FlatQuadColored(dev, 0.5f, red);
            check(isGreen(At(Read(dev), 48, 36)),
                  "B1: NEGATIVE DepthBias pulls the earlier (green) forward -> it WINS over the later red"
                  " [FAILS pre-fix: bias dropped -> red]");
        }

        // ===== C (Phase 17/18): CONSTANT DepthBias on the shaded path (DrawPrimitivesEx) ==============
        {
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadEx(dev, 0.5f, red);
            FlatQuadEx(dev, 0.5f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "C0: DrawPrimitivesEx baseline coplanar (bias 0) -> later (green) wins");

            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadEx(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
            FlatQuadEx(dev, 0.5f, green);
            check(isRed(At(Read(dev), 48, 36)),
                  "C1: DrawPrimitivesEx POSITIVE DepthBias -> green LOSES (red wins)"
                  " [FAILS pre-fix]");
        }

        // ===== D (Phase 7/8): SLOPE-SCALE bias in isolation (DepthBias=0). A sloped green quad over ====
        //       a flat red reference: slope bias pushes the whole sloped quad back so it LOSES.
        {
            // D0 baseline: red flat @0.60, then sloped green (0.30..0.50), slope=0 -> green (shallower) wins.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            WideFlatQuadColored(dev, 0.60f, red);
            SlopedQuadColored(dev, 0.30f, 0.50f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "D0: sloped green (0.30..0.50) over flat red @0.60, SlopeScale=0 -> green wins");

            // D1 biased: same, but SlopeScaleDepthBias pushes the sloped quad back past red -> red wins.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            WideFlatQuadColored(dev, 0.60f, red);
            SetRaster(dev, FillMode::Solid, 0.0f, kSlopeScale);
            SlopedQuadColored(dev, 0.30f, 0.50f, green);
            check(isRed(At(Read(dev), 48, 36)),
                  "D1: SlopeScaleDepthBias on the sloped green pushes it behind red -> red wins"
                  " [FAILS pre-fix: slope bias dropped -> green]");

            // D2 (Phase 6): a FLAT polygon has zero depth slope -> gets NO slope contribution even with a
            // large SlopeScaleDepthBias. Flat green @0.40 over red @0.60 with high SlopeScale -> green
            // still wins (unmoved), proving slope bias is not applied as a constant to flat geometry.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            WideFlatQuadColored(dev, 0.60f, red);
            SetRaster(dev, FillMode::Solid, 0.0f, kSlopeScale);
            FlatQuadColored(dev, 0.40f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "D2: FLAT green @0.40 with high SlopeScale gets zero slope offset -> still wins over red @0.60");
        }

        // ===== E (Phase 19): WIREFRAME fragments receive the same depth bias ==========================
        {
            // Solid red reference @0.5, then a coplanar WIREFRAME green triangle @0.5.
            // Baseline (bias 0): the green wire edge wins at the edge (drawn over red).
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            WideFlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::WireFrame, 0.0f, 0.0f);
            TriColored(dev, 0.5f, green);
            const std::vector<Color> baseEdge = Read(dev);
            const int wireGreenBase = CountIf(baseEdge, isGreen);
            check(wireGreenBase > 0,
                  "E0: coplanar WIREFRAME green edges (bias 0) win over solid red: " +
                  std::to_string(wireGreenBase) + " green edge px");

            // Biased: the SAME wireframe with POSITIVE DepthBias -> wire edges pushed back, LOSE -> no green.
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            WideFlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::WireFrame, kBias, 0.0f);
            TriColored(dev, 0.5f, green);
            const int wireGreenBiased = CountIf(Read(dev), isGreen);
            check(wireGreenBiased == 0,
                  "E1: POSITIVE DepthBias on the WIREFRAME triangle pushes its edges back -> they LOSE (0 green): "
                  + std::to_string(wireGreenBiased) + " [FAILS pre-fix]");
        }

        // ===== F (Phase 21/22): depth test OFF -> bias must NOT change the color result (control) =====
        {
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);   // huge positive bias
            FlatQuadColored(dev, 0.5f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "F0: depth test OFF -> later polygon (green) always wins regardless of DepthBias (control)");
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
        }

        // ===== G (Phase 33): three live RasterizerStates in one frame, each consumes its own bias ======
        //       blue@0.5 (bias 0) -> red@0.5 (NEG bias, pulled forward, WINS) -> green@0.5 (POS bias,
        //       pushed back, LOSES). Post-fix: RED. Pre-fix (bias dropped): all coplanar -> GREEN last.
        {
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, blue);
            SetRaster(dev, FillMode::Solid, -kBias, 0.0f);
            FlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
            FlatQuadColored(dev, 0.5f, green);
            const std::vector<Color> pix = Read(dev);
            check(isRed(At(pix, 48, 36)) && CountIf(pix, isGreen) == 0,
                  "G0: per-draw live bias -- NEG-biased red wins, POS-biased green is rejected -> RED, no green"
                  " [FAILS pre-fix: -> green]");
        }

        // ===== H (Phase 23): Viewport.MinDepth/MaxDepth -- bias applies in window-depth space ==========
        {
            // Depth range [0.2,0.8]; z=0.5 -> window depth 0.2 + 0.5*0.6 = 0.5. Positive bias still flips.
            SetVpDepth(dev, 0, 0, kBBW, kBBH, 0.2f, 0.8f);
            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            FlatQuadColored(dev, 0.5f, green);
            check(isGreen(At(Read(dev), 48, 36)),
                  "H0: custom MinDepth/MaxDepth baseline (bias 0) -> later (green) wins");

            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
            FlatQuadColored(dev, 0.5f, green);
            check(isRed(At(Read(dev), 48, 36)),
                  "H1: DepthBias still flips with custom Viewport MinDepth/MaxDepth -> red wins [FAILS pre-fix]");
            SetVp(dev, 0, 0, kBBW, kBBH);
        }

        // ===== I (Phase 35): RenderTarget2D -- same coplanar bias winner into a bound RT ===============
        {
            const int rtW = 64, rtH = 48;
            RenderTarget2D rt(dev, rtW, rtH, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            SetVp(dev, 0, 0, rtW, rtH);

            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            FlatQuadColored(dev, 0.5f, green);
            const bool rtBaseGreen = isGreen(At(Read(dev, rtW, rtH), rtW / 2, rtH / 2, rtW));

            SetRaster(dev, FillMode::Solid, 0.0f, 0.0f);
            dev.Clear(Color::Black);
            FlatQuadColored(dev, 0.5f, red);
            SetRaster(dev, FillMode::Solid, kBias, 0.0f);
            FlatQuadColored(dev, 0.5f, green);
            const bool rtBiasedRed = isRed(At(Read(dev, rtW, rtH), rtW / 2, rtH / 2, rtW));

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetVp(dev, 0, 0, kBBW, kBBH);
            check(rtBaseGreen, "I0: RenderTarget2D baseline coplanar (bias 0) -> later (green) wins");
            check(rtBiasedRed, "I1: RenderTarget2D POSITIVE DepthBias -> green LOSES (red wins) [FAILS pre-fix]");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    SoftwareDepthBiasTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareDepthBiasTest game;
    game.Run();
    return game.getResult();
}
