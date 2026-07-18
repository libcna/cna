// SPDX-License-Identifier: MS-PL
// audit_net.md remediation (2026-07-18, sixth round): discriminating pixel regression test for
// how EmissiveColor composes into the lit result, across all three EasyGL shader paths that had
// the bug.
//
// FNA's Lighting.fxh ComputeLights() is authoritative:
//
//     result.Diffuse = mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor;
//
// EmissiveColor is ADDED after the DiffuseColor multiply, never multiplied by it. EasyGL's
// EnsureSkinnedProgram / EnsureSkinnedVertexLitProgram / EnsureEnvMapped3DProgram had instead
// computed `(EmissiveColor + lightSum) * DiffuseColor`. Because SkinnedEffect/EnvironmentMapEffect
// pre-fold ambient into emissive (`emissive + ambient*diffuse`, itself FNA-faithful, see
// FillGpuDrawParams), that wrong form evaluated the ambient term as ambient*diffuse^2 - a
// quadratic suppression that crushed dark materials.
//
// WHY THIS BUG SURVIVED EVERY EXISTING TEST, and what makes this one discriminating:
//
//   * examples/easygl_skinned_effect_bones_test.cpp uses DiffuseColor (1,0,0). For components
//     that are exactly 0 or 1, x*x == x, so the double multiply is completely invisible - that
//     test's own comment even spells out the wrong formula and still passes either way.
//   * examples/easygl_environmentmapeffect_amount_zero_test.cpp deliberately pins DiffuseColor to
//     (1,1,1) and says so, explicitly to avoid "whether uDiffuseColor.rgb double-multiplies into
//     the already-diffuse-scaled ambient contribution baked into uEmissiveColor" confounding its
//     result. It named the exact bug and stepped around it.
//
// So this test pins DiffuseColor to (0.25, 0.5, 0.75) - three distinct values strictly between 0
// and 1, where x*x differs sharply from x in every channel - and drives the ambient term alone by
// zeroing all three directional lights. That isolates the emissive composition completely:
//
//     EmissiveColor param = emissive(0,0,0) + ambient(1,1,1) * diffuse = diffuse
//     lightSum            = 0                            (all lights zeroed)
//     CORRECT  litRGB = lightSum * diffuse + emissive = diffuse       = (64, 128, 191)
//     OLD BUG  litRGB = (emissive + lightSum) * diffuse = diffuse^2   = (16,  64, 143)
//
// Every channel differs by 48-64 levels, far outside the +/-20 tolerance, so reinstating the old
// formula fails all three checks unambiguously.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // GPU-compact skinned vertex matching EasyGL's stride-52 layout:
    // pos(12) + normal(12) + uv(8) + weights(16) + indices(4). Same struct
    // examples/easygl_skinned_effect_bones_test.cpp uses, for the same reason.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "SkinnedGpuVertex must be 52 bytes");

    // Strictly between 0 and 1 in every channel, and all three different, so a squared term is
    // unmistakable per-channel. See this file's header comment.
    const Vector3 kDiffuse(0.25f, 0.5f, 0.75f);

    // ambient(1,1,1) * kDiffuse, with zero lights => expected lit RGB == kDiffuse.
    const Color kExpected(64, 128, 191, 255);
    // What the reverted (buggy) formula would produce: kDiffuse squared.
    const Color kBuggyWouldBe(16, 64, 143, 255);
}

class EmissiveAmbientCompositionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;
    bool done_ = false;
    Texture2D whiteTex_;

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }

    void check(const char* label, Color got)
    {
        if (colourMatch(got, kExpected))
        {
            std::printf("[PASS] %s: got=(%d,%d,%d) == light*diffuse + emissive\n", label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected=(%d,%d,%d). "
                        "The old (emissive+light)*diffuse formula would give (%d,%d,%d).\n",
                        label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty(),
                        kExpected.getRProperty(), kExpected.getGProperty(), kExpected.getBProperty(),
                        kBuggyWouldBe.getRProperty(), kBuggyWouldBe.getGProperty(), kBuggyWouldBe.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Zero every directional light so lightSum == 0 and the ambient-folded emissive term is the
    // only contributor - which is exactly what isolates the composition being tested.
    template <typename EffectT>
    static void zeroAllLights(EffectT& fx)
    {
        for (DirectionalLight* light : {&fx.getDirectionalLight0Property(),
                                        &fx.getDirectionalLight1Property(),
                                        &fx.getDirectionalLight2Property()})
        {
            light->setEnabledProperty(true);
            light->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            light->setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            light->setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        }
    }

    std::unique_ptr<TextureCube> makeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                                 CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                                 CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
        {
            cube->SetData(face, &col, 1);
        }
        return cube;
    }

    void DrawSkinned(GraphicsDevice& dev, bool preferPerPixel, const char* label)
    {
        dev.Clear(Color(0, 0, 0, 255));

        // All vertices bound 100% to bone 0 (identity), so skinning is a no-op and only the
        // lighting composition affects the result. Uses the packed 52-byte GPU layout with
        // SetDataRaw + DrawPrimitives, matching examples/easygl_skinned_effect_bones_test.cpp -
        // DrawUserPrimitives does not carry the skinned vertex channels and renders nothing.
        auto v = [](float x, float y) -> SkinnedGpuVertex {
            return {x, y, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0};
        };
        const SkinnedGpuVertex quad[6] = {
            v(-1.0f, 1.0f), v(-1.0f, -1.0f), v(1.0f, -1.0f),
            v(-1.0f, 1.0f), v(1.0f, -1.0f), v(1.0f, 1.0f),
        };

        SkinnedEffect fx(dev);
        fx.setPreferPerPixelLightingProperty(preferPerPixel);
        fx.setTextureProperty(&whiteTex_);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        zeroAllLights(fx);
        fx.SetBoneTransforms({Matrix::getIdentityProperty()});
        fx.setWeightsPerVertexProperty(1);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        check(label, readCenter(dev));
    }

    void DrawEnvMap(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));

        // Deliberately saturated green so any cube-map leak would be obvious; with
        // EnvironmentMapAmount=0 and EnvironmentMapSpecular=0 it must contribute nothing, leaving
        // only the lighting composition under test.
        auto greenCube = makeSolidCube(dev, Color(0, 255, 0, 255));

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            {Vector3(-1, 1, 0), n, Vector2(0, 1)},
            {Vector3(-1, -1, 0), n, Vector2(0, 0)},
            {Vector3(1, -1, 0), n, Vector2(1, 0)},
            {Vector3(-1, 1, 0), n, Vector2(0, 1)},
            {Vector3(1, -1, 0), n, Vector2(1, 0)},
            {Vector3(1, 1, 0), n, Vector2(1, 1)},
        };

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&whiteTex_);
        fx.setEnvironmentMapProperty(greenCube.get());
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        zeroAllLights(fx);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        check("EnvironmentMapEffect (EnvironmentMapAmount=0)", readCenter(dev));
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color kWhite(255, 255, 255, 255);
        whiteTex_ = Texture2D(dev, 1, 1);
        whiteTex_.SetData(&kWhite, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        std::printf("DiffuseColor=(0.25,0.50,0.75) ambient=(1,1,1) all lights zeroed\n");
        std::printf("  correct: light*diffuse + emissive = (%d,%d,%d)\n",
                    kExpected.getRProperty(), kExpected.getGProperty(), kExpected.getBProperty());
        std::printf("  old bug: (emissive+light)*diffuse = (%d,%d,%d)\n",
                    kBuggyWouldBe.getRProperty(), kBuggyWouldBe.getGProperty(), kBuggyWouldBe.getBProperty());

        DrawSkinned(dev, /*preferPerPixel=*/true, "SkinnedEffect per-pixel lighting");
        DrawSkinned(dev, /*preferPerPixel=*/false, "SkinnedEffect vertex lighting");
        DrawEnvMap(dev);

        Exit();
    }

public:
    EmissiveAmbientCompositionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EmissiveAmbientCompositionTest game;
    game.Run();
    return game.getResult();
}
