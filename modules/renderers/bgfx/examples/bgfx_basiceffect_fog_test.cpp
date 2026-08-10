// SPDX-License-Identifier: MS-PL
// Task 888: BasicEffect linear fog pixel integration test — Bgfx renderer.
//
// Direct port of examples/easygl_basiceffect_fog_test.cpp (Task 195), which found this exact gap
// on Vulkan/Bgfx (Task 378): fog was a total GPU no-op for every 3D effect on both renderers.
// Fixed on Bgfx by adding fog uniforms (u_fogColor/u_fogParams, shared by every 3D program) and
// the standard fog blend to every 3D vertex/fragment shader pair -- bgfx has no push-constant
// byte-budget constraint the way Vulkan does, so Bgfx gets full fog coverage across all
// applicable pipelines (colored3d, textured3d, colored_textured3d, lit_textured3d, alpha_test3d,
// alpha_test_colored3d, dual_texture3d) in this same task, unlike Vulkan's partial rollout.
//
// REMED-GFX-005: fog factor corrected to FNA/EasyGL's Task-1111 form
//   geomFraction = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB     = mix(FogColor, geomRGB, geomFraction)
// The prior Task 888 (FogEnd - Z) formula was the mirror image and wrong. Zero-length range
// (FogStart == FogEnd) → fully fogged, matching FNA SetFogVector.
//
// NDC range is [-1, 1] — all quads kept within Z ∈ [-0.9, 0.9].
//
// The old scenes (FogStart=0, FogEnd=1) collapse to a constant under the correct formula
// ((Z+1)/1 ≥ 1 for every Z ≥ 0 → no fog), which is exactly why the mirror survived here. Retargeted
// to FogStart=0, FogEnd=-0.9 (the analog of EasyGL's own -0.9/0.9 design), which discriminates:
// the corrected formula gives no/half/full fog at Z ∈ {0, 0.45, 0.9}, while the mirror gives no fog
// at all three. Asserted pixel values are unchanged; only the inputs (and the mirror-formula
// comment) changed.
//
// Sub-tests (VertexPositionColor stride=16, identity WVP, exercises colored3d):
//   (a) Fog disabled: blue quad at Z=0 → pixel = pure blue
//   (b) Fog enabled: FogStart=0, FogEnd=-0.9, FogColor=red, geom=blue, Z=0.45
//       geomFraction = (0.45-0.9)/(-0.9) = 0.5 → pixel = mix(red,blue,0.5) = (128, 0, 128), tol ±30
//       (mirror gives (0.45+0.9)/0.9 = 1.5 → clamp 1 → blue: the discriminating case)
//   (c) Full fog: FogStart=0, FogEnd=-0.9, Z=0.9 → geomFraction = (0.9-0.9)/(-0.9) = 0
//       → pixel = full fog red   (mirror gives clamp(2.0)=1 → blue)
//
// Bgfx-only note (Task 364/896 finding): RasterizerState::CullNone is required, matching every
// other Bgfx pixel test in this family.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class FogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Full-screen quad at given Z using VertexPositionColor (stride=16).
    static VertexPositionColor quad[6];

    static void makeQuad(float z, Color col)
    {
        quad[0] = { Vector3(-1,  1, z), col };
        quad[1] = { Vector3(-1, -1, z), col };
        quad[2] = { Vector3( 1, -1, z), col };
        quad[3] = { Vector3(-1,  1, z), col };
        quad[4] = { Vector3( 1, -1, z), col };
        quad[5] = { Vector3( 1,  1, z), col };
    }

    Color renderAndRead(GraphicsDevice& dev)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding); this test reads once per frame.
        }
        return got;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        const Color kBlack(  0,   0,   0, 255);
        const Color kBlue (  0,   0, 255, 255);
        const Color kRed  (255,   0,   0, 255);

        auto setupBase = [&](BasicEffect& fx) {
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.VertexColorEnabled = true;
        };

        // ── (a) Fog disabled: blue quad at Z=0 → pure blue ───────────────
        {
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(false);
            fx.Apply();

            makeQuad(0.0f, kBlue);
            Color got = renderAndRead(dev);
            const bool isBlue = got.getBProperty() > 200
                             && got.getRProperty() < 50
                             && got.getGProperty() < 50;
            check(isBlue, "(a) fog OFF: blue quad → pure blue", got, kBlue);
        }

        // ── (b) Fog 50%: Z=0.45, FogStart=0, FogEnd=-0.9, FogColor=red ──
        // geomFraction = (0.45 + (-0.9))/((-0.9)-0) = (-0.45)/(-0.9) = 0.5
        // pixel = mix(red,blue,0.5) = (128, 0, 128) ± 30
        {
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogStartProperty(0.0f);
            fx.setFogEndProperty(-0.9f);
            fx.Apply();

            makeQuad(0.45f, kBlue);
            Color got = renderAndRead(dev);
            const int r = got.getRProperty(), g = got.getGProperty(), b = got.getBProperty();
            const bool isMix = std::abs(r - 128) <= 30
                            && g < 30
                            && std::abs(b - 128) <= 30;
            const Color kMix(128, 0, 128, 255);
            check(isMix, "(b) fog 50%: Z=0.45 → purple mix", got, kMix);
        }

        // ── (c) Full fog: FogStart=0, FogEnd=-0.9, Z=0.9 → geomFraction=0 → red ─
        // geomFraction = clamp((0.9 + (-0.9))/((-0.9)-0), 0, 1) = clamp(0, 0, 1) = 0
        // pixel = mix(red,blue,0) = red  (Z=0.9 is within NDC range [-1,1])
        {
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogStartProperty(0.0f);
            fx.setFogEndProperty(-0.9f);
            fx.Apply();

            makeQuad(0.9f, kBlue);
            Color got = renderAndRead(dev);
            const bool isRed = got.getRProperty() > 200
                            && got.getGProperty() < 50
                            && got.getBProperty() < 50;
            check(isRed, "(c) full fog: FogEnd=-0.9, Z=0.9 → full red", got, kRed);
        }

        Exit();
    }

public:
    FogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

VertexPositionColor FogTest::quad[6];

int main()
{
    FogTest game;
    game.Run();
    return game.getResult();
}
