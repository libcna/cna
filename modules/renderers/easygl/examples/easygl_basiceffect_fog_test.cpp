// SPDX-License-Identifier: MS-PL
// Task 195: BasicEffect linear fog pixel integration test — EasyGL.
// Formula corrected under Task 1111 (see that task's plans/plan_graphics.md row): the previous
// (FogEnd-Z)/(FogEnd-FogStart) falloff was never actually equivalent to FNA's real
// EffectHelpers.SetFogVector/Common.fxh ComputeFogFactor dot product, even for FogStart<FogEnd —
// it happened to give the same answer at this test's own Z=0 midpoint but was wrong everywhere
// else, confirmed by the oracle scene fog_gradient_quad's own worked derivation.
//
// Real formula (World=View=Identity, so the FogVector dot product reduces to a plain scalar):
//   fnaFogFactor = saturate(scale*(z+FogStart)), scale = 1/(FogStart-FogEnd)
//   finalRGB = lerp(geomRGB, FogColor, fnaFogFactor)
// EasyGL's own vFogFactor is 1-fnaFogFactor (mix(FogColor,geomRGB,vFogFactor) convention), which
// simplifies to clamp((Z+FogEnd)/(FogEnd-FogStart), 0, 1).
//
// A real, easy-to-miss consequence (the same one fog_gradient_quad.scene's own comment
// discovered): with View=Identity, FogStart is NOT "the near/unfogged boundary" the way the
// property name might suggest — which boundary reads as fogged depends on the sign relationship
// above, not on which one is literally named Start vs End. This test picks FogStart=-0.9/
// FogEnd=0.9 (matching every sibling *_fog_test.cpp) specifically because that combination gives
// a genuine, non-degenerate gradient; FogStart=0 configurations (this file's own pre-fix values)
// collapse to a constant (always fully unfogged for any Z>=0) and cannot demonstrate a gradient
// at all once the real formula is used.
//
// NDC range is [-1, 1] — all quads kept within Z ∈ [-0.9, 0.9].
//
// Sub-tests (VertexPositionColor stride=16, identity WVP, FogColor=red, geom=blue):
//   (a) Fog disabled: blue quad at Z=0 → pixel = pure blue
//   (b) Fog enabled: FogStart=-0.9, FogEnd=0.9, Z=0 (halfway)
//       vFogFactor = (0+0.9)/(0.9-(-0.9)) = 0.5 → pixel = mix(red,blue,0.5) = (128,0,128) ± 30
//   (c) Full fog: FogStart=-0.9, FogEnd=0.9, Z=-0.9 (at FogStart)
//       vFogFactor = (-0.9+0.9)/(0.9-(-0.9)) = 0 → pixel = full fog red
//       (Z=FogStart is where this scene is FULLY fogged, not unfogged — see header note above)
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

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

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
            dev.Clear(kBlack);
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(false);
            fx.Apply();

            makeQuad(0.0f, kBlue);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            const bool isBlue = got.getBProperty() > 200
                             && got.getRProperty() < 50
                             && got.getGProperty() < 50;
            check(isBlue, "(a) fog OFF: blue quad → pure blue", got, kBlue);
        }

        // ── (b) Fog 50%: Z=0, FogStart=-0.9, FogEnd=0.9, FogColor=red ────
        // vFogFactor = (0+0.9)/(0.9-(-0.9)) = 0.5
        // pixel = mix(red,blue,0.5) = (128, 0, 128) ± 30
        {
            dev.Clear(kBlack);
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogStartProperty(-0.9f);
            fx.setFogEndProperty(0.9f);
            fx.Apply();

            makeQuad(0.0f, kBlue);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            const int r = got.getRProperty(), g = got.getGProperty(), b = got.getBProperty();
            const bool isMix = std::abs(r - 128) <= 30
                            && g < 30
                            && std::abs(b - 128) <= 30;
            const Color kMix(128, 0, 128, 255);
            check(isMix, "(b) fog 50%: Z=0.5 → purple mix", got, kMix);
        }

        // ── (c) Full fog: FogStart=-0.9, FogEnd=0.9, Z=-0.9 (at FogStart) → red ──
        // vFogFactor = clamp((-0.9+0.9)/(0.9-(-0.9)), 0, 1) = clamp(0, 0, 1) = 0
        // pixel = mix(red,blue,0) = red — Z=FogStart is the FULLY fogged boundary here, not
        // FogEnd (see the file header's note on this sign convention).
        {
            dev.Clear(kBlack);
            BasicEffect fx(dev);
            setupBase(fx);
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setFogStartProperty(-0.9f);
            fx.setFogEndProperty(0.9f);
            fx.Apply();

            makeQuad(-0.9f, kBlue);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            const bool isRed = got.getRProperty() > 200
                            && got.getGProperty() < 50
                            && got.getBProperty() < 50;
            check(isRed, "(c) full fog: Z=FogStart=-0.9 → full red", got, kRed);
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
