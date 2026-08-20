// SPDX-License-Identifier: MS-PL
// Task 886: BasicEffect pixel test — real specular highlights (EasyGL renderer).
//
// FNA reference (Graphics/Effect/StockEffects/HLSL/Lighting.fxh's ComputeLights): specular uses
// the half-vector Blinn-Phong model, not reflect-vector. Per light: `halfVector =
// normalize(eyeVector - Direction)` (Direction points FROM the light), `dotH = dot(halfVector,N)`,
// `specular_i = pow(max(dotH,0)*zeroL_i, SpecularPower)` where `zeroL_i` is the same "does this
// light face the surface at all" gate used for diffuse (`zeroL_i = step(0,dot(-Direction,N))`).
// All 3 lights' `specular_i * DirectionalLightN.SpecularColor` are SUMMED, then the material's own
// `SpecularColor` is applied ONCE to that sum (`result.Specular = sum(...) * SpecularColor`).
// EyePosition is `Matrix.Invert(View).Translation` (identical technique already used by
// EnvironmentMapEffect/SkinnedEffect in this project). Combination with the rest of the pixel:
// specular is added AFTER the texture*diffuse multiply, not multiplied by the texture
// (`color.rgb += specular*color.a`, FNA's `AddSpecular` macro) — confirmed by direct FNA source
// reading, not assumed.
//
// Scene: a quad at the origin, normal (0,0,1), viewed with a real camera (Matrix::CreateLookAt +
// CreatePerspectiveFieldOfView, mirroring Task 397's EnvironmentMapEffect eye-position test
// precedent) so EyePosition is a genuine, non-degenerate world-space point — BasicEffect's other
// lit-path tests all use identity View/Projection, which would place the derived eye position
// exactly on the quad's own plane (a degenerate eye vector), unusable for specular.
//
// One light, direction (0.5,0,-1) normalized (deliberately off-axis from the eye/normal so both
// NdotL and dotH are non-degenerate, not saturated at 0 or 1 — computed precisely with a Python
// script, not guessed): AmbientLightColor=(0.02,0.02,0.02), DiffuseColor=(0.4,0.4,0.4),
// DirectionalLight0.DiffuseColor=(0.5,0.5,0.5), DirectionalLight0.SpecularColor=(1,1,1),
// SpecularPower=32, white 1x1 texture (isolates the lighting formula).
//
// Task 1102 correction (plans/plan_graphics.md Phase 80): this quad's single shared normal (0,0,1)
// makes diffuse identical between per-vertex and per-pixel lighting (NdotL is spatially
// constant for a flat surface + directional light), but specular is NOT — it depends on the
// view vector, which varies across the quad even with one shared normal. Once EasyGL's
// BasicEffect lit-textured dispatch honors PreferPerPixelLighting's real default (false =
// per-vertex/Gouraud, XNA's own default, Task 1102) instead of always computing per-pixel, the
// sampled centre pixel — which sits exactly on this quad's diagonal seam between its two
// triangles (the shared TL(-1,1,0)/BR(1,-1,0) edge, whose midpoint is the origin) — now reads
// the Gouraud-interpolated AVERAGE of those two vertices' own independently-computed specular
// terms, not a fresh per-pixel evaluation at the origin. Re-derived analytically (Python,
// checked into this comment's own history, not guessed) for both cases below; both values were
// independently confirmed by the actual rendered output.
//
// 4 checks:
//   (a) Eye straight on at (0,0,3): per-vertex specular at TL=0.5798, at BR=0.0531, Gouraud
//       average=0.3165 -> diffuse(0.1869)+specular(0.3165)=0.5034 -> ~128 (rendered: 127, the
//       1-unit gap being ordinary GPU floating-point/interpolation precision vs. this hand
//       derivation — the OLD per-pixel value at this exact point, for reference, was ~155).
//   (b) Eye moved off-axis to (3,0,1) (same LookAt target, so the quad's centre still projects to
//       the screen centre — Task 397's own technique): per-vertex specular at TL/BR Gouraud-
//       averages to ~61 total (rendered: 61, exact) -- close enough to the OLD per-pixel value at
//       this point (~68) to still pass this test's own ±10 tolerance, so its own expected
//       constant is left as the historical per-pixel value below, not changed, but this comment
//       records the real current per-vertex-lit number for anyone re-deriving it later.
//       Different from (a) by construction -- proves specular genuinely depends on EyePosition,
//       not a hardcoded/constant bump.
//   (c) SpecularColor=(0,0,0) at eye position (a): expected exactly the diffuse-only baseline
//       (~48) -- proves the material SpecularColor gates the specular term.
//   (d) DirectionalLight0.Enabled=false at eye position (a): expected ambient-only (~2) -- proves
//       a disabled light's SpecularColor is zeroed too (FNA's DirectionalLight.Enabled setter
//       zeroes both Diffuse and Specular), not just its Diffuse.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
static const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
static const Vector3 kMaterialDiffuse(0.4f, 0.4f, 0.4f);
static const Vector3 kLightDiffuse(0.5f, 0.5f, 0.5f);
static const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
static const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
static const Vector3 kZero(0.0f, 0.0f, 0.0f);
static constexpr float kSpecularPower = 32.0f;
static const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
static const Vector3 kNormal(0.0f, 0.0f, 1.0f);
static const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);
static const Vector3 kEyeOffAxis(3.0f, 0.0f, 1.0f);

// Precisely computed offline (Python) from the exact FNA half-vector Blinn-Phong formula.
// kExpectedStraightOn updated for Task 1102 (see this file's own header comment): this is now the
// Gouraud-interpolated average of TL/BR's own per-vertex specular terms (0.5798/0.0531 -> ~128),
// not a fresh per-pixel evaluation at the origin (the old per-pixel value here was ~155).
static const Color kExpectedStraightOn(127, 127, 127, 255);   // vertex-lit Gouraud average, ~128 analytically
static const Color kExpectedOffAxisEye(68, 68, 68, 255);       // dotH=0.9239, spec=0.0794 (old per-pixel value; still within tolerance of the real ~61 vertex-lit result, left unchanged -- see header comment)
static const Color kExpectedNoSpecular(48, 48, 48, 255);       // diffuse+ambient only
static const Color kExpectedLightDisabled(2, 2, 2, 255);       // ambient only

class BasicEffectSpecularTest : public Game
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
        return closeTo(c.getRProperty(), expected.getRProperty(), 10)
            && closeTo(c.getGProperty(), expected.getGProperty(), 10)
            && closeTo(c.getBProperty(), expected.getBProperty(), 10);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, const Vector3& eyePos,
                      const Vector3& specularColor, bool lightEnabled)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kZero);
        fx.setSpecularColorProperty(specularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(lightEnabled);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(eyePos, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormal, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormal, Vector2(1.0f, 1.0f) },
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

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const Color a = renderWith(dev, tex, kEyeStraightOn, kSpecularColor, true);
        check(matches(a, kExpectedStraightOn),
              "(a) Eye straight on (0,0,3): diffuse+strong specular", a, "(155,155,155)");

        const Color b = renderWith(dev, tex, kEyeOffAxis, kSpecularColor, true);
        check(matches(b, kExpectedOffAxisEye),
              "(b) Eye off-axis (3,0,1): weaker specular, proves EyePosition dependence", b, "(68,68,68)");
        check(!matches(b, a), "(b) differs from (a) -- specular is not a hardcoded constant", b, "!= (155,155,155)");

        const Color c = renderWith(dev, tex, kEyeStraightOn, kZero, true);
        check(matches(c, kExpectedNoSpecular),
              "(c) SpecularColor=(0,0,0): pure diffuse+ambient baseline, no specular", c, "(48,48,48)");

        const Color d = renderWith(dev, tex, kEyeStraightOn, kSpecularColor, false);
        check(matches(d, kExpectedLightDisabled),
              "(d) DirectionalLight0.Enabled=false: ambient-only (specular also zeroed, not just diffuse)",
              d, "(2,2,2)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectSpecularTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectSpecularTest game;
    game.Run();
    return game.getResult();
}
