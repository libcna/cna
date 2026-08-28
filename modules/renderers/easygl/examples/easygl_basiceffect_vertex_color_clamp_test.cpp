// SPDX-License-Identifier: MS-PL
// plans/plan_fx.md FX-123: Direct3D 9 clamps a vertex shader's colour output registers (oD0/oD1)
// to [0,1] BEFORE the rasterizer interpolates them, so real XNA's own VSBasicVertexLighting hands
// a SATURATED colour to the rasterizer even though Lighting.fxh never writes a saturate(). EasyGL's
// per-vertex-lit program carries the lit and specular RGB in plain varyings, and a plain varying is
// clamped nowhere -- so before FX-123 a per-vertex sum above 1 interpolated UNCLAMPED and the
// triangle came out brighter than D3D9's, with a different gradient rather than a rounding
// difference. FX-122 fixed the same D3D9 semantic in MojoShader's compiled-effect path; this is the
// built-in effect path, and SAMPLE-046 (Graphics3DSample) is what found it: that sample agrees with
// real XNA to 99.99 % with any ONE of its three directional lights on, and drops to 90.31 % with
// all three, because only the accumulated sum crosses 1.
//
// The scene is built so that clamped and unclamped are far apart and computable by hand:
//
//   AmbientLightColor = (1,1,1), DiffuseColor = (1,1,1), one directional light, diffuse (1,1,1),
//   specular black. So the per-vertex lit value is simply  1 + max(dot(N,-L), 0).
//
//   The quad's LEFT vertices face the light (dot(N,-L) = 1 -> lit 2.0) and its RIGHT vertices are
//   perpendicular to it (dot(N,-L) = 0 -> lit 1.0). The lit value therefore varies only along X,
//   identically in both triangles, so the diagonal seam is irrelevant and the horizontal centre
//   reads the midpoint of the two edges:
//
//     D3D9 / XNA (clamped):  min(2,1) and min(1,1) -> 1.0 and 1.0 -> midpoint 1.0
//     unclamped (the bug):   2.0     and 1.0       ->               midpoint 1.5
//
//   The fragment shader multiplies that by the texture, so the texture must NOT be white or both
//   answers saturate to 255 and the test proves nothing. With a 0.4 grey texture (102/255):
//
//     clamped   -> 0.4 * 1.0 = 0.400 -> 102
//     unclamped -> 0.4 * 1.5 = 0.600 -> 153
//
// Two checks:
//   (a) the centre reads the CLAMPED value, which is what D3D9 produces;
//   (b) it is not the unclamped value -- stated separately so a failure says which of the two
//       the renderer actually produced instead of only "not 102".
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

// 0.4 in 8-bit. Deliberately NOT white: with a white texture both the clamped and the unclamped
// answer saturate to 255 and the difference this test exists to see disappears.
static const Color kGrey(102, 102, 102, 255);

static const Vector3 kOne(1.0f, 1.0f, 1.0f);
static const Vector3 kLightDirection(0.0f, 0.0f, -1.0f); // travelling towards -Z, i.e. at the quad
static const Vector3 kNormalAtLight(0.0f, 0.0f, 1.0f);   // dot(N, -L) = 1
static const Vector3 kNormalAcross(0.0f, 1.0f, 0.0f);    // dot(N, -L) = 0
static const Vector3 kEye(0.0f, 0.0f, 3.0f);

// 0.4 * 1.0 and 0.4 * 1.5, rounded to 8 bits.
static constexpr int kClampedLevel = 102;
static constexpr int kUnclampedLevel = 153;

class BasicEffectVertexColorClampTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

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

    static bool isLevel(const Color& c, int level)
    {
        return closeTo(c.getRProperty(), level, 6)
            && closeTo(c.getGProperty(), level, 6)
            && closeTo(c.getBProperty(), level, 6);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kGrey, 1);

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        // XNA's own default, and the path this test is about: lighting once per vertex, then
        // Gouraud-interpolated -- which is where the clamp applies.
        fx.setPreferPerPixelLightingProperty(false);
        fx.setAmbientLightColorProperty(kOne);
        fx.setDiffuseColorProperty(kOne);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(kLightDirection);
        fx.DirectionalLight0.setDiffuseColorProperty(kOne);
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEye, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        // Left vertices face the light, right vertices are perpendicular to it, so the lit value
        // varies only along X and both triangles agree about the horizontal centre.
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormalAtLight, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormalAtLight, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormalAcross,  Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormalAtLight, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormalAcross,  Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormalAcross,  Vector2(1.0f, 1.0f) },
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

        check(isLevel(got, kClampedLevel),
              "(a) per-vertex lit colour is saturated before interpolation, as D3D9's oD0 is",
              got, "(102,102,102)");
        check(!isLevel(got, kUnclampedLevel),
              "(b) it is NOT the unclamped midpoint an unsaturated varying would give",
              got, "not (153,153,153)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BasicEffectVertexColorClampTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectVertexColorClampTest game;
    game.Run();
    return game.getResult();
}
