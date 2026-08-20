// SPDX-License-Identifier: MS-PL
// Task 900: EnvironmentMapEffect linear fog pixel integration test — EasyGL renderer.
//
// REAL BUG FOUND AND FIXED by writing this test: `EnvironmentMapEffect::FillGpuDrawParams()`
// never forwarded `FogEnabled`/`FogColor`/`FogStart`/`FogEnd` to the GPU at all — fog was a total
// no-op for `EnvironmentMapEffect` regardless of what the user set, on EVERY renderer including
// EasyGL, despite the effect having a complete, correct `IEffectFog`/`FogVector` implementation
// in `OnApply()`. Fixed by forwarding the 4 fields in `FillGpuDrawParams()` (mirroring
// `BasicEffect`'s identical pattern), plus adding the same fog uniforms/blend formula EasyGL's
// other 5 shader variants already have to `EnsureEnvMapped3DProgram()`'s shader (previously zero
// fog code there at all).
//
// Formula corrected under Task 1111 (see plans/plan_graphics.md): the previous (FogEnd-Z)/
// (FogEnd-FogStart) falloff was never actually equivalent to FNA's real fog dot product, only
// coincidentally right at this test's own Z=0 midpoint. Real formula (World=View=Identity):
//   vFogFactor = clamp((Z+FogEnd)/(FogEnd-FogStart), 0, 1)   (raw object-space Z)
//   finalRGB   = mix(FogColor, geomRGB, vFogFactor)
// With View=Identity, FogStart is NOT the "near/unfogged" boundary the property name might
// suggest -- see fog_gradient_quad.scene's own comment / Task 1111 for the full derivation.
// FogStart=0 configurations (this file's pre-fix values) collapse to a constant with the real
// formula, so this test now uses FogStart=-0.9/FogEnd=0.9 (matching every sibling *_fog_test.cpp).
//
// EnvironmentMapAmount=0 (Task 393's own isolation technique) removes the cube map's
// contribution entirely, and EnvironmentMapSpecular=0 removes its additive specular term, so the
// pre-fog color reduces to exactly EmissiveColor*DiffuseColor(default 1,1,1)*texColor(white,
// identity) = EmissiveColor -- isolating fog cleanly from the reflection math.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

class EnvironmentMapEffectFogTest : public Game
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

    static bool matches(const Color& c, const Color& expected, int tol = 30)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    std::unique_ptr<TextureCube> makeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
            cube->SetData(face, &col, 1);
        return cube;
    }

    Color renderQuad(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube, float z,
                      bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd)
    {
        EnvironmentMapEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, z), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, z), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, z), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, z), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, z), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, z), n, Vector2(1.0f, 1.0f) },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
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
        dev.SetDepthTestEnabled(false);
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
        // GraphicsDevice's real default RasterizerState is pushed to every renderer,
        // this quad's winding is culled unless explicitly disabled.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);
        auto cube = makeSolidCube(dev, Color(0, 255, 0, 255)); // green -- would leak if Amount!=0

        // (a) Fog disabled: blue quad at Z=0 -> pure blue.
        const Color gotOff = renderQuad(dev, tex, *cube, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue quad -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0, FogStart=-0.9, FogEnd=0.9, FogColor=red -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, tex, *cube, 0.0f, true, Vector3(1, 0, 0), -0.9f, 0.9f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogStart=-0.9, FogEnd=0.9, Z=-0.9 (at FogStart) -> vFogFactor=0 -> pure red.
        const Color gotFull = renderQuad(dev, tex, *cube, -0.9f, true, Vector3(1, 0, 0), -0.9f, 0.9f);
        check(matches(gotFull, kRed), "(c) full fog: Z=FogStart=-0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapEffectFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapEffectFogTest game;
    game.Run();
    return game.getResult();
}
