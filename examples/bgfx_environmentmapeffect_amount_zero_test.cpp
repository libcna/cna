// SPDX-License-Identifier: MS-PL
// Task 393: verify EnvironmentMapEffect ignores the cube map entirely when
// EnvironmentMapAmount=0 (Bgfx backend). See
// examples/easygl_environmentmapeffect_amount_zero_test.cpp for the full derivation and
// the note on CNA's additive vs. FNA's real lerp-based blend formula (they coincide at
// EnvironmentMapAmount=0, so this test doesn't discriminate that divergence -- flagged
// for Task 394). Task 278's own note that Bgfx "has no GPU readback API" predates the
// real GetBackBufferData()-based readback established by later Bgfx tests in this
// project (Tasks 379/383-389) -- this test uses real pixel verification, not a smoke test.
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 backends that actually matches FNA's
// real CullCounterClockwiseFace default, so it silently culls the standard NDC quad winding
// used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly -- worked around here identically to prior Bgfx tests.
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

class BgfxEnvironmentMapAmountZeroTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
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

        const Color kTex(200, 100, 50, 255);
        const Color kGreenCube(0, 255, 0, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto greenCube = makeSolidCube(dev, kGreenCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(greenCube.get());
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }

        check(colourMatch(got, Color(100, 50, 25, 255)),
              "EnvironmentMapAmount=0 with green cube → cube ignored, texture-only result",
              got, Color(100, 50, 25, 255));

        // REMED-GFX-007: discriminating case for the emissive-composition defect. With white
        // DiffuseColor (the case above) the buggy `(emissive+lightSum)*diffuse` and FNA's correct
        // `lightSum*diffuse + emissive` coincide, so it cannot detect the bug. Here DiffuseColor is
        // (0.5,0.5,0.5) and EmissiveColor (0.5,0.5,0.5), no lights (lightSum=0), EnvironmentMapAmount=0
        // (isolates litRGB), tex=(200,100,50):
        //   correct: litRGB = 0*0.5 + 0.5 = 0.5 ; baseColor = 0.5*tex → (100,50,25)
        //   buggy  : litRGB = (0.5+0)*0.5 = 0.25; baseColor = 0.25*tex → (50,25,13)
        // So this sub-test FAILS on the pre-fix shader and PASSES after.
        {
            EnvironmentMapEffect fx2(dev);
            fx2.setTextureProperty(&tex);
            fx2.setEnvironmentMapProperty(greenCube.get());
            fx2.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            fx2.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            fx2.setEnvironmentMapAmountProperty(0.0f);
            fx2.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx2.setWorldProperty(Matrix::getIdentityProperty());
            fx2.setViewProperty(Matrix::getIdentityProperty());
            fx2.setProjectionProperty(Matrix::getIdentityProperty());

            Color got2(0, 0, 0, 0);
            for (int i = 0; i < 20; ++i)
            {
                dev.Clear(Color(0, 0, 0, 255));
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                fx2.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
                got2 = readCenter(dev);
                if (got2.getRProperty() != 0 || got2.getGProperty() != 0 || got2.getBProperty() != 0)
                    break; // skip blank/black frames
            }

            check(colourMatch(got2, Color(100, 50, 25, 255)),
                  "non-white Diffuse + non-zero Emissive → emissive added UNSCALED (lightSum*Diffuse + Emissive)",
                  got2, Color(100, 50, 25, 255));
        }

        Exit();
    }

public:
    BgfxEnvironmentMapAmountZeroTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxEnvironmentMapAmountZeroTest game;
    game.Run();
    return game.getResult();
}
