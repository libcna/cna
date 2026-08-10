// SPDX-License-Identifier: MS-PL
// Task 891: verify EnvironmentMapEffect's base cube-map lerp target is scaled by the combined
// texture x diffuse alpha, mirroring Task 395's own discriminating technique but for the BASE
// lerp term instead of the specular term (shared source across EasyGL/Vulkan/Bgfx, mirroring
// Task 950's one-source-3-renderers pattern).
//
// FNA's real PSEnvMap/PSEnvMapSpecular pixel shaders compute:
//   envmap = SAMPLE_CUBEMAP(EnvironmentMap, pin.EnvCoord) * color.a;   // color.a = combinedAlpha
//   color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb);        // envmap.rgb already scaled
// i.e. combinedAlpha (texture alpha x DiffuseColor alpha) scales the WHOLE envmap sample before
// either its RGB (the base lerp target) or its alpha (the specular term, already fixed by Task
// 395) is used. CNA's shaders correctly scaled the specular term but left the base lerp's
// envSample.rgb unscaled -- `mix(baseColor, envSample.rgb, Amount)` instead of FNA's real
// `mix(baseColor, envSample.rgb * combinedAlpha, Amount)`.
//
// EnvironmentMapAmount=1.0 makes mix() return its second argument unconditionally, cleanly
// isolating the base lerp term from baseColor; EnvironmentMapSpecular=(0,0,0) zeroes the
// (already-correct, Task 395-covered) specular addition so it can't confound this test. The
// cube map itself is fully opaque (alpha=255) so envSample.a stays 1 -- only the texture/diffuse
// alpha varies between cases, isolating exactly this task's own fix.
//
// (a) Opaque effect (Alpha=1.0): NOT discriminating -- combinedAlpha=1 either way. Sanity check
//     that EnvironmentMapAmount=1.0 correctly shows the cube map at all.
// (b) Translucent effect (Alpha=0.5): discriminating.
//       CubeColor = (200,100,50)
//       FNA's alpha-scaled formula:  (200,100,50) * 0.5              = (100,50,25)
//       CNA's unscaled (bug) formula: (200,100,50) * 1.0 (unscaled)  = (200,100,50)
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

class EnvironmentMapAlphaScaledLerpTest : public Game
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
                      const VertexPositionNormalTexture (&quad)[6], float effectAlpha)
    {
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAlphaProperty(effectAlpha);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
            // GraphicsDevice's real default RasterizerState is pushed to every renderer,
            // this quad's winding is culled unless explicitly disabled.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black readback frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Color kTex(255, 255, 255, 255);
        const Color kCube(200, 100, 50, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeSolidCube(dev, kCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        // (a) Opaque effect: NOT discriminating (combinedAlpha=1 either way), sanity check only.
        Color a = renderWith(dev, tex, *cube, quad, 1.0f);
        check(colourMatch(a, Color(200, 100, 50, 255)),
              "(a) opaque effect, cube map base lerp visible (not discriminating)",
              a, Color(200, 100, 50, 255));

        // (b) Translucent effect: discriminating. FNA's real formula scales the base lerp's
        // envSample.rgb by combinedAlpha; CNA's unscaled (bug) formula would instead repeat
        // case (a)'s result.
        Color b = renderWith(dev, tex, *cube, quad, 0.5f);
        check(colourMatch(b, Color(100, 50, 25, 255)),
              "(b) translucent effect -> base lerp's cube sample scaled by combinedAlpha (FNA)",
              b, Color(100, 50, 25, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapAlphaScaledLerpTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapAlphaScaledLerpTest game;
    game.Run();
    return game.getResult();
}
