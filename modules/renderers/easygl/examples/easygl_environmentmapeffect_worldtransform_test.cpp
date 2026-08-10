// SPDX-License-Identifier: MS-PL
// Task 398: verify EnvironmentMapEffect's normal transform under a non-uniform-scale World
// matrix (EasyGL renderer).
//
// XNA content ships `WorldInverseTranspose` for exactly this reason: transforming a normal by
// the World matrix directly is only correct when World is a pure rotation/uniform-scale/
// translation. Under NON-UNIFORM scale, the correct normal transform is
// `transpose(inverse(World3x3))`, not `World3x3` directly.
//
// Auditing CNA's actual dispatch found this bug is real and confirmed on 2 of 3 renderers:
//   - EasyGL: computed the "normal matrix" as the raw upper-left 3x3 of World on the CPU side
//     (`BindDrawParams()`), never inverting/transposing it -- WRONG under non-uniform scale.
//   - Vulkan: computes `transpose(inverse(mat3(world)))` directly in the vertex shader --
//     already CORRECT, no fix needed.
//   - Bgfx: transforms the normal by `mul(u_world, vec4(a_normal,0.0))` directly in the vertex
//     shader -- WRONG under non-uniform scale, same shape as EasyGL's bug.
// Notably, `EnvironmentMapEffect::OnApply()` ALREADY computes the correct
// `WorldInverseTranspose` and stores it in its own `EffectParameter` (for API/content-pipeline
// fidelity) -- but this correct value was never forwarded into the actual GPU dispatch path on
// either buggy renderer, which independently (and incorrectly) derived their own "normal
// matrix" from the raw World matrix instead.
//
// To discriminate the two formulas, this test assigns every vertex a NON-axis-aligned normal
// `(0, 1, 1)/sqrt(2)` (a synthetic normal decoupled from the quad's actual flat geometry --
// EnvironmentMapEffect's shader has no way to validate that a supplied normal is geometrically
// consistent with its triangle, so this is a valid way to isolate the normal-transform math)
// and applies `World = CreateScale(1, 1, 20)` -- a non-uniform scale along Z, the very axis this
// test's normal has a large component along. The scale factor is deliberately large (not a
// modest 4x) so the resulting reflection vectors land deep inside their respective cube faces
// (~10:1 dominant-to-secondary component ratio) rather than near a face edge/corner, where a
// 1x1-texel-per-face cube map's seamless bilinear filtering would blend 2-3 adjacent faces
// together and blur the discriminating signal (an early attempt at this test with a milder 4x
// scale hit exactly this artifact, reading back a blended yellow/green instead of either pure
// face color).
//
//   Correct (transpose(inverse(diag(1,1,20)))): scales the normal's Z component by 1/20, giving
//     (0, 0.9989, 0.0499) after normalization -- Y-dominant, ~20:1.
//   Buggy (raw World multiply): scales the normal's Z component by 20, giving
//     (0, 0.0499, 0.9989) after normalization -- Z-dominant, the OPPOSITE dominant axis.
//
// With the camera looking straight on from (0,0,3) (eyeVector=(0,0,1)), these transformed
// normals produce reflection vectors with OPPOSITE-signed dominant Z components:
//   Correct → reflDir≈(0, 0.0998, -0.9950) → dominant NegativeZ face (yellow), ~10:1 ratio.
//   Buggy   → reflDir≈(0, 0.0998,  0.9950) → dominant PositiveZ face (blue), unambiguously
//             different from the correct result, same ~10:1 ratio.
//
// EnvironmentMapAmount=1, FresnelFactor=0 (disabling Task 396's Fresnel weighting, which would
// otherwise interact with this test's own view-angle setup) and EmissiveColor=Zero with no
// lights enabled (zeroing the lit/textured contribution) isolate the output to exactly the
// sampled cube face color.
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

class EnvironmentMapWorldTransformTest : public Game
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

    std::unique_ptr<TextureCube> makeDistinctCube(GraphicsDevice& dev)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        Color posX(255, 0, 0, 255);   // red
        Color negX(0, 255, 255, 255); // cyan
        Color posY(0, 255, 0, 255);   // green
        Color negY(255, 0, 255, 255); // magenta
        Color posZ(0, 0, 255, 255);   // blue
        Color negZ(255, 255, 0, 255); // yellow
        cube->SetData(CubeMapFace::PositiveX, &posX, 1);
        cube->SetData(CubeMapFace::NegativeX, &negX, 1);
        cube->SetData(CubeMapFace::PositiveY, &posY, 1);
        cube->SetData(CubeMapFace::NegativeY, &negY, 1);
        cube->SetData(CubeMapFace::PositiveZ, &posZ, 1);
        cube->SetData(CubeMapFace::NegativeZ, &negZ, 1);
        return cube;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
        // GraphicsDevice's real default RasterizerState is pushed to every renderer,
        // this quad's winding is culled unless explicitly disabled.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kTex(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeDistinctCube(dev);

        // Synthetic normal (0,1,1)/sqrt(2), deliberately NOT axis-aligned and NOT
        // perpendicular to the quad's own flat geometry -- decoupled on purpose to isolate the
        // normal-transform math (see file header).
        const Vector3 n(0.0f, 0.70710678f, 0.70710678f);
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
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f);
        // Non-uniform scale along Z -- the very axis this test's normal has a large
        // component along, so a wrong (non-inverse-transposed) normal transform is exposed.
        fx.setWorldProperty(Matrix::CreateScale(1.0f, 1.0f, 20.0f));
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        Color got = readCenter(dev);

        check(colourMatch(got, Color(255, 255, 0, 255)),
              "non-uniform World scale → normal transformed via inverse-transpose (NegativeZ, yellow)",
              got, Color(255, 255, 0, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapWorldTransformTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapWorldTransformTest game;
    game.Run();
    return game.getResult();
}
