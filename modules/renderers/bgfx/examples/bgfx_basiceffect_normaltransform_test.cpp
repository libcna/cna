// SPDX-License-Identifier: MS-PL
// Task 892: verify BasicEffect's lit-textured normal transform under a real camera (Bgfx
// renderer). Opened by Task 398's audit of EnvironmentMapEffect's own (separate) normal-matrix
// bug; confirmed here as a real, independently-verified bug in BasicEffect's lit-textured path.
//
// Bug: `vs_lit_textured3d.sc` transformed the vertex normal by the FULL `u_wvp` (World * View *
// Projection) matrix: `v_normal = normalize(mul(u_wvp, vec4(a_normal,0.0)).xyz)`. This is wrong
// under ANY non-identity View/Projection, not just non-uniform World scale (a stricter bug than
// Task 398's EnvironmentMapEffect one, which only manifested under non-uniform World scale) --
// baking View/Projection into the normal corrupts its direction relative to world-space lighting
// regardless of World. Every prior Bgfx lit-textured test (Tasks 364-885) used identity View/
// Projection, which makes WVP≈World and completely masks this bug -- discovered while building
// Task 886's BasicEffect specular test, the first Bgfx lit-textured test to use a real camera.
//
// Fixed by computing the correct inverse-transpose of World's upper-left 3x3 on the CPU side
// (via the cofactor/det shortcut, mirroring EnvironmentMapEffect's own Task 398 fix exactly) and
// passing it as a new `u_normalMatrix` uniform, used instead of `u_wvp` for the normal.
//
// Scene: World=Identity (deliberately -- proves the bug needs only a non-identity camera, not
// non-uniform scale), a real camera via Matrix::CreateLookAt + CreatePerspectiveFieldOfView
// (mirroring Task 397/398's own precedent), one off-axis light. Expected value derived precisely
// (Python) from the correct World-space formula: (Ambient+NdotL*LightDiffuse)*DiffuseColor, no
// specular (SpecularColor=0, isolating this test from Task 886's separate specular feature).
//
// Per Task 364's finding (tracked as Task 896, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 renderers that actually matches FNA's real
// CullCounterClockwiseFace default, so it silently culls the standard NDC quad winding used
// throughout this pixel-test family unless RasterizerState::CullNone is set explicitly.
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
static const Vector3 kZero(0.0f, 0.0f, 0.0f);
static const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
static const Vector3 kNormal(0.0f, 0.0f, 1.0f);
static const Vector3 kEye(0.0f, 0.0f, 3.0f);

// (Ambient + NdotL*LightDiffuse) * DiffuseColor, NdotL = dot(N,-lightDir) = 0.8944 -- precisely
// computed (Python) from the correct World-space formula, not the WVP-corrupted one.
static const Color kExpected(48, 48, 48, 255);

class BgfxBasicEffectNormalTransformTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kZero);
        fx.setSpecularColorProperty(kZero); // isolate from Task 886's specular

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEye, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
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

        check(matches(got, kExpected),
              "Real camera (non-identity View/Projection): normal transformed correctly via "
              "inverse-transpose, not corrupted by the WVP matrix",
              got, "(48,48,48)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxBasicEffectNormalTransformTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxBasicEffectNormalTransformTest game;
    game.Run();
    return game.getResult();
}
