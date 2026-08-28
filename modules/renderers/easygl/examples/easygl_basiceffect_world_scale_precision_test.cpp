// SPDX-License-Identifier: MS-PL
// plans/plan_fx.md FX-124: EasyGL's per-pixel-lit fragment shaders normalize a WORLD-SPACE view
// vector, normalize(uEyePosition - vWorldPos). GLSL ES's `mediump` guarantees only fp16 RANGE
// (about +/-65504), and normalize() computes dot(v, v) first -- so an eye a few thousand world
// units from the geometry overflows, inversesqrt returns 0, the view direction collapses to the
// zero vector, and the specular term is wrong everywhere. Nothing errors: the model still draws,
// still responds to the camera and still honours AmbientLightColor, so the frame reads as a
// plausible lit render. SAMPLE-046 (Graphics3DSample) puts its camera 3500 units out and that is
// how this was found.
//
// The oracle is SCALE INVARIANCE, which needs no reference image and no hand-derived constant.
// Blinn-Phong depends only on directions: N, the light direction and the normalized view vector.
// Multiplying the whole scene -- vertex positions, eye position, near and far -- by a constant k
// changes no direction at all, so the shaded result at the corresponding pixel must be IDENTICAL.
// It is a property of the maths, not of a particular scale.
//
// The scene is deliberately specular-dominant (ambient and diffuse near zero, specular white)
// because specular is the term that dies: a diffuse-only scene stays correct at mediump and would
// pass while broken. At mediump the k = 1000 leg loses its highlight and the centre pixel drops by
// far more than the tolerance; at highp the two legs agree.
//
// Three checks:
//   (a) the unit-scale leg has a real specular highlight -- otherwise (c) compares two black
//       frames and can never fail, which is the trap a scale-invariance test walks into;
//   (b) the 1000x leg has one too;
//   (c) the two agree.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
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
// Specular-dominant on purpose: specular is the term the precision loss destroys.
static const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
static const Vector3 kMaterialDiffuse(0.02f, 0.02f, 0.02f);
static const Vector3 kLightDiffuse(0.02f, 0.02f, 0.02f);
static const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
static const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
static constexpr float kSpecularPower = 8.0f;
static const Vector3 kLightDirRaw(0.35f, 0.0f, -1.0f);
static const Vector3 kNormal(0.0f, 0.0f, 1.0f);

// A highlight worth calling a highlight. The ambient+diffuse floor here is ~0.0004, i.e. 0/255.
static constexpr int kHighlightFloor = 40;

class BasicEffectWorldScalePrecisionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const char* detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label, detail);
        ok ? ++pass_ : ++fail_;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

    /// Renders the same scene with every LENGTH multiplied by @p scale. Every direction, and
    /// therefore every shading term, is unchanged by construction.
    Color renderAtScale(GraphicsDevice& dev, Texture2D& tex, float scale)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        // The per-fragment path: this is the one that carries a world-space position across the
        // stage boundary and normalizes it in the fragment shader.
        fx.setPreferPerPixelLightingProperty(true);
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
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f * scale),
                                                Vector3::Zero,
                                                Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f * scale, 100.0f * scale));

        const float s = scale;
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-s,  s, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3(-s, -s, 0.0f), kNormal, Vector2(0.0f, 0.0f) },
            { Vector3( s, -s, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3(-s,  s, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3( s, -s, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3( s,  s, 0.0f), kNormal, Vector2(1.0f, 1.0f) },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            // The real default RasterizerState culls this quad's winding, same finding as the
            // specular and PreferPerPixelLighting tests next to this one.
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

        const Color unitScale = renderAtScale(dev, tex, 1.0f);
        const Color bigScale = renderAtScale(dev, tex, 1000.0f);

        char detail[256];

        std::snprintf(detail, sizeof detail, "centre = %d, floor %d",
                      unitScale.getGProperty(), kHighlightFloor);
        check(unitScale.getGProperty() > kHighlightFloor,
              "(a) the unit-scale leg really does have a specular highlight", detail);

        std::snprintf(detail, sizeof detail, "centre = %d, floor %d",
                      bigScale.getGProperty(), kHighlightFloor);
        check(bigScale.getGProperty() > kHighlightFloor,
              "(b) the 1000x-scale leg has one too", detail);

        const int delta = std::abs(unitScale.getGProperty() - bigScale.getGProperty());
        std::snprintf(detail, sizeof detail, "unit = %d, 1000x = %d, |delta| = %d (tolerance 4)",
                      unitScale.getGProperty(), bigScale.getGProperty(), delta);
        check(delta <= 4,
              "(c) shading is invariant under a uniform scale of the whole scene", detail);

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectWorldScalePrecisionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectWorldScalePrecisionTest game;
    game.Run();
    return game.getResult();
}
