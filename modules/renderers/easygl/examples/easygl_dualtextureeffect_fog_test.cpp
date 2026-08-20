// SPDX-License-Identifier: MS-PL
// Task 388: DualTextureEffect fog behavior (EasyGL renderer).
//
// REAL BUG FOUND AND FIXED by writing this test: `DualTextureEffect::FillGpuDrawParams()` never
// forwarded `FogEnabled`/`FogColor`/`FogStart`/`FogEnd` to the GPU at all — fog was a total no-op
// for `DualTextureEffect` regardless of what the user set. Unlike Task 378's `AlphaTestEffect` fix
// (where the shared per-stride shader already had fog infra), `DualTextureEffect` uses its own
// dedicated shader (`EnsureDualTextured3DProgram()`, not one of the shared per-stride programs)
// which had **no fog uniforms at all**. Fixed on both sides: added
// `uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd` uniforms plus the standard fog blend (mirroring
// `EnsureTextured3DProgram()`'s identical pattern exactly), then forwarded the 4 fog fields in
// `FillGpuDrawParams()` (mirroring `AlphaTestEffect`'s Task 378 fix). Formula corrected under
// Task 1111 (see plans/plan_graphics.md): `vFogFactor = clamp((z+FogEnd)/(FogEnd-FogStart), 0, 1)` /
// `mix(FogColor, original, vFogFactor)` — the original `(FogEnd-z)/(FogEnd-FogStart)` was never
// actually equivalent to FNA's real fog dot product, only coincidentally right at `z=0`.
//
// `Texture2=gray(128,128,128)` deliberately cancels Task 383's `color.rgb *= 2` doubling factor
// (`1(white)*2*0.502(gray)≈1.004`) so the pre-fog material color reduces to ~`DiffuseColor`
// directly, isolating this test's own variable (fog) the same way Task 385/386 isolated theirs.
//
// A 3-point Z-sweep (`z=FogStart` full fog, `z=FogEnd` no fog, `z=0` half fog) proves the blend
// is a genuine interpolation, not just an on/off switch — same methodology as Task 378. A real,
// easy-to-miss consequence of the real formula: with `View=Identity`, `FogStart` is the FULLY
// FOGGED boundary here, not the unfogged one the property name might suggest — see
// `fog_gradient_quad.scene`'s own comment / Task 1111 for the full derivation. `World`/`View`/
// `Projection` are all Identity, so raw vertex Z is `gl_Position.z` directly (must stay within
// OpenGL's valid clip-space NDC range `[-1,1]` or the primitive gets frustum-clipped away).
//
// CONFIRMED, NOT FIXED HERE: Vulkan and Bgfx have zero fog GPU implementation for any 3D effect
// (Task 888, discovered by Task 378) — no test is added for either renderer, matching Task 377/378's
// precedent of not committing a test that would encode a known no-op as a passing assertion.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Vector3 kDiffuse(0.8f, 0.2f, 0.4f);
static const Vector3 kFogColor(0.1f, 0.6f, 0.9f);
// Kept inside OpenGL's valid clip-space NDC range [-1,1] (World/View/Projection are all Identity
// in this test, so raw vertex z IS gl_Position.z).
static constexpr float kFogStart = -0.9f;
static constexpr float kFogEnd   =  0.9f;

// Expected: mix(FogColor, MaterialColor, clamp((z+FogEnd)/(FogEnd-FogStart),0,1)), where
// MaterialColor ≈ white*2*gray(0.502)*DiffuseColor ≈ DiffuseColor (the *2/gray cancellation is
// not exact -- 2*128/255=1.00392 -- so expected material color is (205,51,102), not (204,51,102)).
static const Color kExpectedNoFog(205, 51, 102, 255);   // z=FogEnd:   factor=1, unblended material color
static const Color kExpectedFullFog(26, 153, 230, 255); // z=FogStart: factor=0, pure fog color
static const Color kExpectedHalfFog(115, 102, 166, 255);// z=0:        factor=0.5, halfway blend

class DualTextureFogTest : public Game
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

    Color renderAtZ(GraphicsDevice& dev, Texture2D& texWhite, Texture2D& texGray, float z)
    {
        DualTextureEffect fx(dev);
        fx.setTextureProperty(&texWhite);
        fx.setTexture2Property(&texGray);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setFogEnabledProperty(true);
        fx.setFogColorProperty(kFogColor);
        fx.setFogStartProperty(kFogStart);
        fx.setFogEndProperty(kFogEnd);
        fx.Apply();

        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            { tl, uv0 }, { bl, uv1 }, { br, uv2 },
            { tl, uv0 }, { br, uv2 }, { tr, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
            // GraphicsDevice's real default RasterizerState is pushed to every renderer,
            // this quad's winding is culled unless explicitly disabled.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
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

        const Color kWhite(255, 255, 255, 255);
        const Color kGrayHalf(128, 128, 128, 255);
        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);
        Texture2D texGray(dev, 1, 1);  texGray.SetData(&kGrayHalf, 1);

        const Color noFogGot = renderAtZ(dev, texWhite, texGray, kFogEnd);
        check(matches(noFogGot, kExpectedNoFog),
              "z=0.9 (at FogEnd): unblended material color", noFogGot, "(205,51,102)");

        const Color fullFogGot = renderAtZ(dev, texWhite, texGray, kFogStart);
        check(matches(fullFogGot, kExpectedFullFog),
              "z=-0.9 (at FogStart): pure fog color", fullFogGot, "(26,153,230)");

        const Color halfFogGot = renderAtZ(dev, texWhite, texGray, 0.0f);
        check(matches(halfFogGot, kExpectedHalfFog),
              "z=0 (halfway): 50/50 blend, proves real interpolation not on/off",
              halfFogGot, "(115,102,166)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DualTextureFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DualTextureFogTest game;
    game.Run();
    return game.getResult();
}
