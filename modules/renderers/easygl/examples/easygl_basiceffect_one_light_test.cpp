// SPDX-License-Identifier: MS-PL
// Task 368: BasicEffect pixel test — LightingEnabled=true with a single DirectionalLight
// (EasyGL renderer). Verifies the normal-dependent per-vertex diffuse lighting formula, not just
// a saturated (NdotL=0/1) degenerate case.
//
// FNA reference (Graphics/Effect/StockEffects/BasicEffect.cs OnApply() + HLSL/BasicEffect.fx +
// Lighting.fxh): with LightingEnabled=true and DirectionalLight1/2 both disabled (their real FNA
// default), BasicEffect.cs's oneLight optimization selects shaderIndex 1(no fog)+16(one light)=17
// -> VSBasicOneLight/PSBasicVertexLightingNoFog. Lighting.fxh's ComputeLights(eyeVector,
// worldNormal, 1) computes, per light: `dotL = dot(-Direction, N)`, `diffuse = max(dotL, 0)`
// (the `step(0,dotL)*dotL` idiom clamps negative dot products to zero, i.e. a back-facing surface
// contributes nothing), then `result.Diffuse = diffuse * Light0.DiffuseColor * DiffuseColor.rgb +
// EmissiveColor`. With EmissiveColor at its default (0,0,0) (deferred to Task 369, matching Tasks
// 364-367's precedent) and AmbientLightColor added on top (per ComputeLights' actual formula,
// re-derived directly from FNA source rather than assumed): **expected fragment =
// (AmbientLightColor + DirectionalLight0.DiffuseColor * max(dot(-Light0Direction, N), 0)) *
// DiffuseColor**, with the sampled texture color multiplied in on top (kept at white here so the
// lighting formula itself is isolated, matching Task 365/366's one-variable-at-a-time style).
// SpecularColor/SpecularPower are irrelevant here: DirectionalLight0.SpecularColor is left at its
// default (0,0,0), so the true FNA specular term (`lightSpecular * SpecularColor`) is mathematically
// zero regardless of whether CNA forwards it — this test does not depend on or exercise the known,
// separately-tracked "SpecularColor/SpecularPower/DirectionalLight1/2 not forwarded" gap (Task 369).
//
// REAL BUG FOUND AND FIXED by writing this test: `BasicEffect::FillGpuDrawParams()` forwarded
// `DirectionalLight0`'s `Direction`/`DiffuseColor` to the GPU unconditionally, **never checking
// `DirectionalLight0.Enabled` at all**. In real FNA, `DirectionalLight.Enabled`'s setter
// (`DirectionalLight.cs`) zeroes the light's GPU-facing diffuse/specular parameters when disabled,
// regardless of what `DiffuseColor` is still set to at the C# property level — so a disabled
// light must contribute exactly zero. CNA's `DirectionalLight` class has no such side effect (it's
// a plain flag, consistent with this project's stock effects bypassing FNA's EffectParameter
// reflection pipeline entirely — Task 351/361 finding), so the gating has to happen in
// `FillGpuDrawParams()` itself. Fixed by reading `DirectionalLight0.getEnabledProperty()` and
// substituting `Vector3::Zero` for the forwarded diffuse color when disabled. This bug is common
// C++ code shared by all 3 renderers, so one fix covers all 3.
//
// Uses a non-saturating NdotL=0.5 (45-degree-family angle, not 0 or 1) to prove the actual dot
// product is computed, not just a boolean lit/unlit check; a back-facing normal to prove the
// negative-dot clamp; and DirectionalLight0.Enabled=false (reusing the lit geometry) to prove the
// new Enabled gate. Non-white/non-primary AmbientLightColor (0.1,0.1,0.1), light color
// (1.0,0.6,0.2), and material DiffuseColor (0.5,0.5,1.0) chosen so ambient-only, texture-only, and
// light-color-only failure modes are all numerically distinguishable from the correct result.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kAmbient(0.1f, 0.1f, 0.1f);
static const Vector3 kMaterialDiffuse(0.5f, 0.5f, 1.0f);
static const Vector3 kLightDiffuse(1.0f, 0.6f, 0.2f);
static const Vector3 kLightDir(0.0f, 0.0f, 1.0f);

// dot(-kLightDir, N) = 0.5 -> N.z = -0.5.
static const Vector3 kNormalLit(0.8660254f, 0.0f, -0.5f);
// dot(-kLightDir, N) = -0.5 -> clamped to 0 (back-facing).
static const Vector3 kNormalBackFacing(-0.8660254f, 0.0f, 0.5f);

// Expected: (Ambient + LightDiffuse*0.5) * MaterialDiffuse * 255 = (0.3,0.2,0.2)*255.
static const Color kExpectedLit(77, 51, 51, 255);
// Expected (back-facing, or light disabled): Ambient * MaterialDiffuse * 255 = (0.05,0.05,0.1)*255.
static const Color kExpectedAmbientOnly(13, 13, 26, 255);
// Failure-mode references, used only to prove discriminating power.
// NdotL=1 instead of the real 0.5: (Ambient+LightDiffuse*1)*MaterialDiffuse*255 = (0.55,0.35,0.3)*255.
static const Color kFullySaturated(140, 89, 77, 255);
// Ambient additive term dropped: (LightDiffuse*0.5)*MaterialDiffuse*255 = (0.25,0.15,0.1)*255.
static const Color kAmbientIgnored(64, 38, 26, 255);

class BasicEffectOneLightTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, const Vector3& normal, bool light0Enabled)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.DirectionalLight0.setEnabledProperty(light0Enabled);
        fx.DirectionalLight0.setDirectionProperty(kLightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionNormalTexture q[6] = {
            { tl, normal, uv0 }, { bl, normal, uv1 }, { br, normal, uv2 },
            { tl, normal, uv0 }, { br, normal, uv2 }, { tr, normal, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
            // is culled by the real default RasterizerState once EasyGL pushes it at construction.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const Color litGot = renderWith(dev, tex, kNormalLit, true);
        check(matches(litGot, kExpectedLit),
              "One light: NdotL=0.5 partial lit == (Ambient+Light*0.5)*Diffuse",
              litGot, "(77,51,51)");
        check(!matches(litGot, kFullySaturated),
              "One light: pixel != NdotL=1 saturated (dot product is real, not boolean)",
              litGot, "not (140,89,77)");
        check(!matches(litGot, kAmbientIgnored),
              "One light: pixel != ambient-dropped result (Ambient term is additive)",
              litGot, "not (64,38,26)");

        const Color backGot = renderWith(dev, tex, kNormalBackFacing, true);
        check(matches(backGot, kExpectedAmbientOnly),
              "One light: back-facing normal == ambient-only (negative dot clamped to 0)",
              backGot, "(13,13,26)");

        const Color disabledGot = renderWith(dev, tex, kNormalLit, false);
        check(matches(disabledGot, kExpectedAmbientOnly),
              "One light: DirectionalLight0.Enabled=false == ambient-only (light contributes 0)",
              disabledGot, "(13,13,26)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectOneLightTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectOneLightTest game;
    game.Run();
    return game.getResult();
}
