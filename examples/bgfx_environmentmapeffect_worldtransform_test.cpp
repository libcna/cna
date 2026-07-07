// SPDX-License-Identifier: MS-PL
// Task 398: verify EnvironmentMapEffect's normal transform under a non-uniform-scale World
// matrix (Bgfx backend). See examples/easygl_environmentmapeffect_worldtransform_test.cpp for
// the full derivation and the real bug this test found and fixed: Bgfx's `vs_env_map3d.sc`
// transformed the normal by `mul(u_world, vec4(a_normal,0.0))` directly -- WRONG under
// non-uniform scale, same shape as EasyGL's bug. Fixed by computing the correct
// transpose(inverse(world3x3)) on the CPU side (via the cofactor/det shortcut) and passing it
// as a new `u_normalMatrix` uniform.
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

class BgfxEnvironmentMapWorldTransformTest : public Game
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

        const Color kTex(255, 255, 255, 255);
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeDistinctCube(dev);

        const Vector3 n(0.0f, 0.70710678f, 0.70710678f);
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
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f);
        fx.setWorldProperty(Matrix::CreateScale(1.0f, 1.0f, 20.0f));
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

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

        check(colourMatch(got, Color(255, 255, 0, 255)),
              "non-uniform World scale → normal transformed via inverse-transpose (NegativeZ, yellow)",
              got, Color(255, 255, 0, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxEnvironmentMapWorldTransformTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxEnvironmentMapWorldTransformTest game;
    game.Run();
    return game.getResult();
}
