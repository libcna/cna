// SPDX-License-Identifier: MS-PL
// Task 394: verify EnvironmentMapEffect's cube-map contribution at
// EnvironmentMapAmount=1 (EasyGL renderer).
//
// Task 393 found a real formula-level question: FNA's real pixel shader (PSEnvMap) does
// `color.rgb = lerp(color.rgb, envmap.rgb, EnvironmentMapAmount)` -- at Amount=1 the cube
// map should FULLY REPLACE the lit/textured color. CNA's actual EasyGL shader formula
// (per Task 134/192's own documented derivation) is instead
// `rgb = litRGB*texColor.rgb + envColor*uEnvMapAmount + uEnvMapSpecular` -- an ADDITIVE
// blend, not a lerp.
//
// (a) White cubemap (matches this task's literal title, "strong contribution"): both
//     formulas predict the same clamped-to-white (255,255,255) result -- a genuinely
//     saturated cubemap value can't discriminate the two hypotheses (the same lesson
//     Task 383 learned: 0/1-saturated values hide real formula bugs). Included here only
//     as a sanity check that a strong contribution is visible at all, not as evidence the
//     formula is correct.
// (b) Gray cubemap (128,128,128), deliberately non-saturated, with a genuinely nonzero
//     lit/textured contribution (litRGB*texColor=(100,50,25), same setup as Task 393) --
//     this is the actually-discriminating case:
//       FNA's lerp (Amount=1 fully replaces):  (128,128,128)
//       CNA's additive formula (adds instead):  (100,50,25)+(128,128,128)=(228,178,153)
//     These are unambiguously different, unlike case (a).
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

class EnvironmentMapAmountOneTest : public Game
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
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
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
        const Color kWhiteCube(255, 255, 255, 255);
        const Color kGrayCube(128, 128, 128, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto whiteCube = makeSolidCube(dev, kWhiteCube);
        auto grayCube  = makeSolidCube(dev, kGrayCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        // (a) White cubemap: NOT discriminating (both formulas saturate to white), included
        // only to confirm a strong/visible contribution occurs at Amount=1.
        Color a = renderWith(dev, tex, *whiteCube, quad);
        check(colourMatch(a, Color(255, 255, 255, 255)),
              "(a) white cubemap, Amount=1 → strong contribution (not discriminating)",
              a, Color(255, 255, 255, 255));

        // (b) Gray cubemap: discriminating. FNA's real lerp fully replaces the lit color
        // with (128,128,128); CNA's additive formula would instead give (228,178,153).
        Color b = renderWith(dev, tex, *grayCube, quad);
        check(colourMatch(b, Color(128, 128, 128, 255)),
              "(b) gray cubemap, Amount=1 → FULLY REPLACES lit color (FNA lerp semantics)",
              b, Color(128, 128, 128, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapAmountOneTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapAmountOneTest game;
    game.Run();
    return game.getResult();
}
