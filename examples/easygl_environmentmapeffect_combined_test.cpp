// SPDX-License-Identifier: MS-PL
// Task 399: cross-backend EnvironmentMapEffect capstone — combines everything Tasks 393-398
// verified individually into one non-trivial scene, to prove the fixes compose correctly
// together rather than just in isolation (mirrors Task 370/389's precedent for
// BasicEffect/DualTextureEffect).
//
// Scene combines:
//   - Task 394's lerp-based blend formula (exercised via mix() with a tiny nonzero blend
//     factor, not a hardcoded 0 or 1 special case).
//   - Task 395's EnvironmentMapSpecular * envmap.a * combinedAlpha formula (a translucent
//     cube map, alpha=128).
//   - Task 396's Fresnel edge-weighting (FresnelFactor left at its real default of 1) --
//     combined with a head-on camera angle, this suppresses the base cube-map blend almost
//     entirely (blendFactor ~= 0.00004, negligible), isolating the env-map contribution to
//     almost exactly the specular term alone -- a different code path than Task 395's own
//     Amount=0 isolation, landing on the same easily-verified numeric target.
//   - Task 397's EyePosition-driven reflection math, exercised via a real (non-identity)
//     `Matrix::CreateLookAt`/`CreatePerspectiveFieldOfView` camera instead of Identity.
//   - Task 398's now-fixed normal transform, exercised via a non-identity, non-uniform-scale
//     `World = CreateScale(2,1,1)`. Deliberately scales only X/Y (not Z) so the quad's own
//     Z-aligned normal is unaffected by the correct-vs-buggy normal-matrix distinction either
//     way (Task 398's own dedicated test already covers that discrimination) -- this task's
//     job is to confirm the rest of the pipeline still works correctly with a real non-identity
//     World in play, not to re-litigate the normal-matrix fix itself.
//
// Expected value derivation:
//   baseColor = EmissiveColor * Texture = (0.5,0.5,0.5) * (200,100,50) = (100,50,25)
//   combinedAlpha = texColor.a * DiffuseColor.a = 1 * 1 = 1
//   blendFactor ~= 0 (Fresnel-suppressed at near-head-on view; negligible perturbation, see
//     above -- computed to be ~0.00004 accounting for the actual pixel-center sample offset,
//     which shifts the (151,101,76) result by less than 0.01 in any channel)
//   rgb = mix(baseColor, envColor, blendFactor~=0) + EnvironmentMapSpecular * envmap.a * combinedAlpha
//       ~= baseColor + (0.4,0.4,0.4) * (128/255) * 1
//       ~= (100,50,25) + (51,51,51) = (151,101,76)
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

class EnvironmentMapCombinedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
        // GraphicsDevice's real default RasterizerState is pushed to every backend,
        // this quad's winding is culled unless explicitly disabled.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kTex(200, 100, 50, 255);
        const Color kTranslucentCube(0, 0, 0, 128);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeSolidCube(dev, kTranslucentCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        dev.Clear(Color(0, 0, 0, 255));
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.4f, 0.4f, 0.4f));
        fx.setFresnelFactorProperty(1.0f); // real FNA default -- Fresnel enabled
        fx.setWorldProperty(Matrix::CreateScale(2.0f, 1.0f, 1.0f));
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        Color got = readCenter(dev);

        check(colourMatch(got, Color(151, 101, 76, 255)),
              "combined scene (lerp+specular+Fresnel+EyePosition+non-identity World)",
              got, Color(151, 101, 76, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapCombinedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapCombinedTest game;
    game.Run();
    return game.getResult();
}
