// SPDX-License-Identifier: MS-PL
// Task 886: BasicEffect pixel test — real specular highlights (Bgfx backend).
//
// See examples/easygl_basiceffect_specular_test.cpp for the full FNA-derived half-vector
// Blinn-Phong formula derivation and the 4 checks' precise expected-value computation.
//
// Per Task 364's finding (tracked as Task 896, not fixed there or here): Bgfx's default
// RasterizerState cull state (`BGFX_STATE_CULL_CCW`) is the only one of the 3 backends that
// actually matches FNA's real `CullCounterClockwiseFace` default, so it silently culls the
// standard NDC quad winding used throughout this pixel-test family unless `RasterizerState::
// CullNone` is set explicitly — worked around here identically to Tasks 364-886's own Bgfx tests.
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

static const Color kExpectedStraightOn(155, 155, 155, 255);
static const Color kExpectedOffAxisEye(68, 68, 68, 255);
static const Color kExpectedNoSpecular(48, 48, 48, 255);
static const Color kExpectedLightDisabled(2, 2, 2, 255);

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
            // See Task 364/896 finding: Bgfx's default cull state culls this quad's winding.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
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
