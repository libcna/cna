// SPDX-License-Identifier: MS-PL
// Task 1102 (plans/plan_graphics.md Phase 80 / plans/plan_dx9.md Divergence 1): BasicEffect pixel test --
// PreferPerPixelLighting genuinely selects between two different lighting evaluations (EasyGL
// renderer).
//
// Real XNA 4.0 default: PreferPerPixelLighting=false -> lighting is computed ONCE per vertex
// (VSBasicVertexLighting*) and Gouraud-interpolated across the triangle. true -> lighting is
// re-evaluated per fragment (VSBasicPixelLighting*/PSBasicPixelLighting*). Before this task,
// EasyGL always evaluated per pixel regardless of this flag's value -- the opposite of XNA's own
// default -- and GpuDrawParams didn't even carry the flag at all (plans/plan_graphics.md Task 1100).
//
// Reuses the exact scene from easygl_basiceffect_specular_test.cpp's own "(a) eye straight on"
// case, chosen there (and reused here) because a single shared vertex normal makes DIFFUSE
// lighting spatially constant (dot(N,L) doesn't depend on position for a flat surface + a
// directional light), so this scene is useless for finding a per-vertex-vs-per-pixel gap in
// diffuse alone -- but SPECULAR still varies across the surface even with one shared normal
// (the view vector depends on position), which is exactly why this scene IS discriminating for
// specular. The sampled centre pixel sits exactly on the diagonal seam between this quad's two
// triangles (shared vertices TL=(-1,1,0)/BR=(1,-1,0), whose midpoint is the origin), so:
//   - PreferPerPixelLighting=false (vertex-lit): the centre reads the Gouraud-interpolated
//     AVERAGE of TL's and BR's own independently-computed per-vertex specular terms.
//   - PreferPerPixelLighting=true (pixel-lit): the centre reads a FRESH per-fragment evaluation
//     at the origin itself.
// These are analytically different (confirmed via an offline Python re-derivation of FNA's own
// half-vector Blinn-Phong formula, Lighting.fxh's ComputeLights, not guessed):
//   vertex-lit:  specular(TL)=0.5798, specular(BR)=0.0531, Gouraud average=0.3165
//                -> diffuse(0.1869)+specular(0.3165) = 0.5034 -> ~128 (rendered: 127)
//   pixel-lit:   specular(origin)=0.4199 -> diffuse(0.1869)+specular(0.4199) = 0.6068 -> ~155
//
// 3 checks:
//   (a) Default (PreferPerPixelLighting left at its real XNA default, false): expect the
//       vertex-lit/Gouraud value (~127).
//   (b) PreferPerPixelLighting=true: expect the pixel-lit value (~155) -- the OLD, pre-Task-1102
//       value this renderer always produced regardless of the flag.
//   (c) (a) != (b): proves the flag is a genuine, live dispatch selector, not a decorative no-op.
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
static constexpr float kSpecularPower = 32.0f;
static const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
static const Vector3 kNormal(0.0f, 0.0f, 1.0f);
static const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);

// Analytically re-derived, same values as easygl_basiceffect_specular_test.cpp's own case (a).
static const Color kExpectedVertexLit(127, 127, 127, 255);
static const Color kExpectedPixelLit(155, 155, 155, 255);

class BasicEffectPreferPerPixelLightingTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, bool preferPerPixelLighting)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setPreferPerPixelLightingProperty(preferPerPixelLighting);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(kSpecularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEyeStraightOn, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
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
            // Same real default-RasterizerState culling finding as the specular test's own
            // Task 896 note -- this quad's winding is culled by the real default otherwise.
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

        const Color vertexLit = renderWith(dev, tex, false);
        check(matches(vertexLit, kExpectedVertexLit),
              "(a) PreferPerPixelLighting=false (XNA's real default): Gouraud-interpolated specular",
              vertexLit, "(127,127,127)");

        const Color pixelLit = renderWith(dev, tex, true);
        check(matches(pixelLit, kExpectedPixelLit),
              "(b) PreferPerPixelLighting=true: fresh per-fragment specular",
              pixelLit, "(155,155,155)");

        check(!matches(vertexLit, pixelLit),
              "(c) (a) differs from (b) -- PreferPerPixelLighting is a real dispatch selector",
              vertexLit, "!= (155,155,155)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectPreferPerPixelLightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectPreferPerPixelLightingTest game;
    game.Run();
    return game.getResult();
}
