// SPDX-License-Identifier: MS-PL
// Task 393: verify EnvironmentMapEffect ignores the cube map entirely when
// EnvironmentMapAmount=0 (EasyGL renderer).
//
// CNA's EasyGL env-map shader formula (confirmed against Task 134/192's own derivation):
//   litRGB = (uEmissiveColor + uLight0Diffuse * NdotL) * uDiffuseColor.rgb
//   rgb    = litRGB * texColor.rgb + envColor * uEnvMapAmount + uEnvMapSpecular
//
// Note: this is an ADDITIVE blend, not FNA's real `lerp(color.rgb, envmap.rgb,
// blendFactor)` (Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx's PSEnvMap).
// The two formulas coincide exactly at EnvironmentMapAmount=0 (both reduce to the
// lit/textured color with zero cube-map contribution) and at 0 they're
// indistinguishable, so this test doesn't exercise that divergence -- flagged here for
// whoever picks up Task 394 (`EnvironmentMapAmount=1` should fully REPLACE the lit color
// with the cube map under FNA's real lerp; CNA's current additive formula would instead
// ADD the cube map on top, a real formula-level discrepancy worth verifying there).
//
// DiffuseColor deliberately kept at its default (1,1,1) to avoid a second, unrelated open
// question (whether uDiffuseColor.rgb double-multiplies into the already-diffuse-scaled
// ambient contribution baked into uEmissiveColor) from confounding this test's own result.
//
// EnvironmentMap is set to a highly saturated, distinctive pure green -- totally unlike
// the expected texture-based result -- so any nonzero leak of the cube map's RGB would be
// immediately, unmistakably visible.
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

class EnvironmentMapAmountZeroTest : public Game
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
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
        // GraphicsDevice's real default RasterizerState is pushed to every renderer,
        // this quad's winding is culled unless explicitly disabled.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kBlack(0, 0, 0, 255);
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

        dev.Clear(kBlack);
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(greenCube.get());
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        // litRGB = EmissiveColor(0.5,0.5,0.5) * DiffuseColor(1,1,1) = (0.5,0.5,0.5)
        // rgb = litRGB * texColor(200,100,50)/255 + greenCube*0 + 0 = (100,50,25)
        Color got = readCenter(dev);
        check(colourMatch(got, Color(100, 50, 25, 255)),
              "EnvironmentMapAmount=0 with green cube → cube ignored, texture-only result",
              got, Color(100, 50, 25, 255));

        Exit();
    }

public:
    EnvironmentMapAmountZeroTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    EnvironmentMapAmountZeroTest game;
    game.Run();
    return game.getResult();
}
