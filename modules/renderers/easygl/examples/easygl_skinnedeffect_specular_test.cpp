// SPDX-License-Identifier: MS-PL
// Task 894: SkinnedEffect pixel test — real specular highlights (EasyGL renderer). See
// examples/easygl_basiceffect_specular_test.cpp for the full FNA-reference half-vector
// Blinn-Phong derivation (Lighting.fxh's ComputeLights) — SkinnedEffect uses the exact same
// formula and (with an identity bone palette + identity World) the exact same expected numbers.
//
// Before this task, SkinnedEffect had zero specular infrastructure: no SpecularColor/SpecularPower
// forwarding, no per-light SpecularColor forwarding, no EyePosition/world-position plumbing in any
// renderer's skinned shader at all.
//
// Uses an identity bone palette (weight=1 on bone 0, which defaults to Identity), isolating
// skinning from the specular formula under test.
//
// Task 1102b correction (plans/plan_graphics.md Phase 80): this test never calls
// SetPreferPerPixelLightingProperty(), so it always exercised whatever this renderer's
// SkinnedEffect dispatch treated as its default. Before Task 1102b, that default was
// unconditionally per-pixel (the opposite of real XNA's own default, per-vertex/Gouraud) --
// after Task 1102b, it correctly matches XNA's own PreferPerPixelLighting=false default. Case
// (a)'s sampled centre pixel sits exactly on this quad's diagonal seam between its two triangles
// (shared TL(-1,1,0)/BR(1,-1,0) edge), so it now reads the Gouraud-interpolated AVERAGE of those
// two vertices' own specular terms (~127 analytically, matching
// easygl_basiceffect_preferperpixellighting_test.cpp's own identical derivation for the same
// scene) instead of a fresh per-pixel evaluation at the origin (the old per-pixel value here was
// ~155). Case (b)'s off-axis eye position still passes within this test's own existing ±10
// tolerance under the new default and was left unchanged, same precedent
// easygl_basiceffect_specular_test.cpp's own Task 1102 update already established. Cases (c)/(d)
// are unaffected (no specular term / light disabled, respectively).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout (Task 123's own convention).
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

static const Color kWhite(255, 255, 255, 255);
static const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
static const Vector3 kMaterialDiffuse(0.4f, 0.4f, 0.4f);
static const Vector3 kLightDiffuse(0.5f, 0.5f, 0.5f);
static const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
static const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
static const Vector3 kZero(0.0f, 0.0f, 0.0f);
static constexpr float kSpecularPower = 32.0f;
static const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
static const float kNx = 0.0f, kNy = 0.0f, kNz = 1.0f;
static const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);
static const Vector3 kEyeOffAxis(3.0f, 0.0f, 1.0f);

// Precisely computed offline (Python) from the exact FNA half-vector Blinn-Phong formula --
// identical numbers to BasicEffect's own specular test (same shared Lighting.fxh formula).
// kExpectedStraightOn updated for Task 1102b (see this file's own header comment): now the
// Gouraud-interpolated average of TL/BR's own per-vertex specular terms (~128 analytically,
// rendered 126), not a fresh per-pixel evaluation at the origin (old per-pixel value was ~155).
static const Color kExpectedStraightOn(126, 126, 126, 255);   // vertex-lit Gouraud average
static const Color kExpectedOffAxisEye(68, 68, 68, 255);       // dotH=0.9239, spec=0.0794 (old per-pixel value; still within tolerance of the vertex-lit result, left unchanged -- see header comment)
static const Color kExpectedNoSpecular(48, 48, 48, 255);       // diffuse+ambient only
static const Color kExpectedLightDisabled(2, 2, 2, 255);       // ambient only

class SkinnedEffectSpecularTest : public Game
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

        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kZero);
        fx.setSpecularColorProperty(specularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(lightEnabled);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(eyePos, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();

        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  kNx,kNy,kNz,  0,1,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  kNx,kNy,kNz,  0,0,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,0,  1,0,0,0,  0,0,0,0 },
            { -1,  1, 0,  kNx,kNy,kNz,  0,1,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  kNx,kNy,kNz,  1,0,  1,0,0,0,  0,0,0,0 },
            {  1,  1, 0,  kNx,kNy,kNz,  1,1,  1,0,0,0,  0,0,0,0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
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
    SkinnedEffectSpecularTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SkinnedEffectSpecularTest game;
    game.Run();
    return game.getResult();
}
