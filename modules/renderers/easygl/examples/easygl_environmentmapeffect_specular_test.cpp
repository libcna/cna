// SPDX-License-Identifier: MS-PL
// Task 395: verify EnvironmentMapEffect's EnvironmentMapSpecular contribution
// (EasyGL renderer).
//
// FNA's real PSEnvMapSpecular pixel shader does:
//   envmap = SAMPLE_CUBEMAP(EnvironmentMap, pin.EnvCoord) * color.a;
//   color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb);
//   color.rgb += EnvironmentMapSpecular * envmap.a;
// i.e. EnvironmentMapSpecular is scaled by the cube map's ALPHA channel (further scaled by
// the combined texture/diffuse alpha, "color.a"), not added as a flat constant. CNA's actual
// shader formula (per Task 394's own derivation) instead did `rgb = ... + uEnvMapSpecular` --
// a flat additive term that never reads the cube map's alpha channel at all.
//
// EnvironmentMapAmount is deliberately set to 0 here so the cube map's RGB contribution
// (still an open, deliberately-deferred nuance -- see Task 891) cannot confound this test;
// only EnvironmentMapSpecular's own alpha-scaling is exercised.
//
// (a) Opaque cubemap (alpha=255): NOT discriminating -- flat-add and alpha-scaled formulas
//     agree here (scaling by 1.0 changes nothing). Included only as a sanity check that
//     EnvironmentMapSpecular is visible at all.
// (b) Translucent cubemap (alpha=128), deliberately non-saturated: discriminating.
//       baseColor = litRGB*texColor = (100,50,25)   (same setup as Tasks 393/394)
//       FNA's alpha-scaled formula:  (100,50,25) + (0.4,0.4,0.4)*(128/255) ≈ (151,101,76)
//       CNA's flat-additive formula: (100,50,25) + (0.4,0.4,0.4)*1.0       =  (202,152,127)
//     These are unambiguously different.
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

class EnvironmentMapSpecularTest : public Game
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube,
                      const VertexPositionNormalTexture (&quad)[6])
    {
        dev.Clear(Color(0, 0, 0, 255));
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.4f, 0.4f, 0.4f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return readCenter(dev);
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

        const Color kTex(200, 100, 50, 255);
        const Color kOpaqueCube(0, 0, 0, 255);
        const Color kTranslucentCube(0, 0, 0, 128);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto opaqueCube      = makeSolidCube(dev, kOpaqueCube);
        auto translucentCube = makeSolidCube(dev, kTranslucentCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        // (a) Opaque cubemap: NOT discriminating (alpha=1 either way), sanity check only.
        Color a = renderWith(dev, tex, *opaqueCube, quad);
        check(colourMatch(a, Color(202, 152, 127, 255)),
              "(a) opaque cubemap, EnvironmentMapSpecular visible (not discriminating)",
              a, Color(202, 152, 127, 255));

        // (b) Translucent cubemap: discriminating. FNA's real formula scales
        // EnvironmentMapSpecular by the cube map's alpha channel; CNA's flat-additive
        // formula would instead ignore it and repeat case (a)'s result.
        Color b = renderWith(dev, tex, *translucentCube, quad);
        check(colourMatch(b, Color(151, 101, 76, 255)),
              "(b) translucent cubemap → EnvironmentMapSpecular scaled by cubemap alpha (FNA)",
              b, Color(151, 101, 76, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapSpecularTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapSpecularTest game;
    game.Run();
    return game.getResult();
}
