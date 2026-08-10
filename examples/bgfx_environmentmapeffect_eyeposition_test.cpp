// SPDX-License-Identifier: MS-PL
// Task 397: verify EnvironmentMapEffect's reflection vector responds to EyePosition
// (Bgfx renderer). See examples/easygl_environmentmapeffect_eyeposition_test.cpp for the full
// derivation.
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 renderers that actually matches FNA's
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

class BgfxEnvironmentMapEyePositionTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube,
                      const VertexPositionNormalTexture (&quad)[6],
                      const Matrix& view, const Matrix& proj)
    {
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);

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
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Color kTex(255, 255, 255, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeDistinctCube(dev);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        Matrix proj = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

        Matrix viewA = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Color a = renderWith(dev, tex, *cube, quad, viewA, proj);
        check(colourMatch(a, Color(0, 0, 255, 255)),
              "(a) eye straight on (0,0,3) → PositiveZ face (blue)",
              a, Color(0, 0, 255, 255));

        Matrix viewB = Matrix::CreateLookAt(Vector3(5.0f, 0.0f, 0.5f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Color b = renderWith(dev, tex, *cube, quad, viewB, proj);
        check(colourMatch(b, Color(0, 255, 255, 255)),
              "(b) eye off-axis (5,0,0.5) → NegativeX face (cyan), different from (a)",
              b, Color(0, 255, 255, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxEnvironmentMapEyePositionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxEnvironmentMapEyePositionTest game;
    game.Run();
    return game.getResult();
}
