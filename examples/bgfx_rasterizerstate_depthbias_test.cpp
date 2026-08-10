// SPDX-License-Identifier: MS-PL
// Task 767: verify RasterizerState.DepthBias is applied on Bgfx via the new vertex-shader
// Z-offset emulation (bgfx has no native polygon-offset mechanism, unlike D3D9/Vulkan/GL --
// see BgfxRenderer::SetDepthBiasUniform and the u_depthBias uniform added to every 3D
// vertex shader). SlopeScaleDepthBias is deliberately NOT emulated (project-owner decision,
// 2026-07-10, following the constant-DepthBias-only scope agreed for Bgfx specifically) --
// this file's 3rd check documents that gap as informational only, matching this project's
// established precedent for known, undisputed limitations (e.g. OcclusionQuery.PixelCount(),
// Tasks 814/815).
//
// Same "shadow acne"-style coplanar-redraw methodology as examples/easygl_depth_bias_test.cpp
// (Task 767's EasyGL half) and examples/vulkan_depth_bias_test.cpp (Task 328), restructured into
// per-check RunCheck() passes (own Clear+Draw+Read retry loop) since Bgfx's own GetBackBufferData
// only reliably reflects the first read per rendered frame (Task 406 finding) -- unlike the
// EasyGL/Vulkan versions, which render all scenarios into one frame and read multiple points.
//
// Method: DepthStencilState.DepthBufferFunction is forced to CompareFunction::Less (XNA's real
// default LessEqual can't discriminate a coplanar redraw -- a second draw at equal depth always
// passes regardless of bias). A red triangle A is drawn first with no bias, writing its depth. A
// green triangle B with EXACTLY the same geometry is drawn second with the scenario's own
// RasterizerState. Under LESS, B's second draw at equal depth fails (centre stays red) unless a
// negative bias pulls B's depth toward the camera so it passes (centre turns green).
//
// Check A — flat,   DepthBias = 0             → RED   (B fails the equal-depth test)
// Check B — flat,   DepthBias = -1e6          → GREEN (constant bias pulls B in front)
// Check C — tilted, SlopeScaleDepthBias = -2e3 (DepthBias = 0) → informational only, NOT a
//   pass/fail assertion: Bgfx does not emulate SlopeScaleDepthBias, so this is expected to stay
//   RED (matching Check A) regardless of the slope; documents the known gap rather than silently
//   omitting it.
//
// Exit code 0 = both hard checks (A, B) PASS, 1 = either FAILs. Check C never affects the result.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kBackground(0, 0, 0, 255);

    bool IsRed(const Color& c)
    {
        return c.getRProperty() >= 200 && c.getGProperty() <= 30 && c.getBProperty() <= 30;
    }
    bool IsGreen(const Color& c)
    {
        return c.getGProperty() >= 200 && c.getRProperty() <= 30 && c.getBProperty() <= 30;
    }

    // CW-winding triangle covering most of NDC space (matches the wireframe/cullmode Bgfx tests'
    // own reference geometry). tilted=false -> all vertices at z=0; tilted=true -> top at
    // z=-0.6, base at z=+0.6, so depth slopes top-to-bottom across the triangle.
    void DrawTri(GraphicsDevice& dev, bool tilted, const Color& col)
    {
        const float zt = tilted ? -0.6f : 0.0f;
        const float zb = tilted ?  0.6f : 0.0f;
        const VertexPositionColor verts[3] = {
            { Vector3( 0.0f,  0.8f, zt), col },
            { Vector3( 1.0f, -0.8f, zb), col },
            { Vector3(-1.0f, -0.8f, zb), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }
}

class BgfxRasterizerStateDepthBiasTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    Color RunCheck(GraphicsDevice& dev, bool tilted, const RasterizerState& rsB)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBackground);
            dev.setBlendStateProperty(BlendState::Opaque);

            DepthStencilState dss;
            dss.setDepthBufferFunctionProperty(CompareFunction::Less);
            dev.setDepthStencilStateProperty(dss);

            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
            fx.Apply();

            dev.setRasterizerStateProperty(RasterizerState());
            DrawTri(dev, tilted, Color(255, 0, 0, 255));

            dev.setRasterizerStateProperty(rsB);
            DrawTri(dev, tilted, Color(0, 255, 0, 255));

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        RasterizerState rsNoBias;                          // DepthBias = 0
        RasterizerState rsConst;  rsConst.setDepthBiasProperty(-1000000.0f);
        RasterizerState rsSlope;  rsSlope.setSlopeScaleDepthBiasProperty(-2000.0f);

        const Color a = RunCheck(dev, false, rsNoBias);
        const Color b = RunCheck(dev, false, rsConst);
        const Color c = RunCheck(dev, true,  rsSlope);

        int passCount = 0;
        const bool okA = IsRed(a);
        const bool okB = IsGreen(b);
        std::printf("[%s] DepthBias=0 (flat): centre=(%d,%d,%d) expected RED\n",
                    okA ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        if (okA) ++passCount;
        std::printf("[%s] DepthBias=-1e6 (flat): centre=(%d,%d,%d) expected GREEN\n",
                    okB ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());
        if (okB) ++passCount;
        std::printf("(informational only, see file header) SlopeScaleDepthBias=-2000 (tilted): "
                    "centre=(%d,%d,%d) -- not emulated on Bgfx, expected to stay RED\n",
                    c.getRProperty(), c.getGProperty(), c.getBProperty());

        std::printf("=== %d/2 hard checks PASS ===\n", passCount);
        result_ = (passCount == 2) ? 0 : 1;
        Exit();
    }

public:
    BgfxRasterizerStateDepthBiasTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRasterizerStateDepthBiasTest game;
    game.Run();
    return game.getResult();
}
