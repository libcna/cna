// SPDX-License-Identifier: MS-PL
// Task 134 (extended Task 192): EasyGL EnvironmentMapEffect pixel integration test.
//
// Shader formula:
//   litRGB   = (uEmissiveColor + uLight0Diffuse * NdotL) * uDiffuseColor.rgb
//   rgb      = litRGB * texColor.rgb + envColor * uEnvMapAmount + uEnvMapSpecular
//   FragColor = vec4(rgb, uDiffuseColor.a * texColor.a)
//
// Four sub-tests verify that EmissiveColor, EnvironmentMapAmount, and
// EnvironmentMapSpecular each independently affect the output pixel:
//
//   (a) EmissiveColor=red,   envAmount=0, envSpecular=0 → red
//       (baseline: litRGB=(1,0,0)*(1,1,1)=(1,0,0), rgb=(1,0,0)*(1,1,1)+0+0=(1,0,0))
//   (b) EmissiveColor=green, envAmount=0, envSpecular=0 → green
//       (same path, different emissive colour — verifies setter)
//   (c) EmissiveColor=0,     envAmount=0, envSpecular=(0,0,1) → blue
//       (rgb = 0 + 0 + (0,0,1))
//   (d) EmissiveColor=0,     envAmount=1, blue cube map,  envSpecular=0 → blue
//       (rgb = 0 + (0,0,1)*1 + 0; all six cube faces are solid blue)
//
// Uses VertexPositionNormalTexture (stride=32). World/View/Projection = identity.
// Normal = (0,0,1), diffuseColor = white, texture0 = white 1×1.
//
// Exit code 0 = all PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

class EnvMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
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

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Fills a 1×1 TextureCube with a single solid colour on all 6 faces.
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
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Shared resources.
        const Color kBlack(  0,   0,   0, 255);
        const Color kRed  (255,   0,   0, 255);
        const Color kGreen(  0, 255,   0, 255);
        const Color kBlue (  0,   0, 255, 255);
        const Color kWhite(255, 255, 255, 255);

        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        auto whiteCube = makeSolidCube(dev, kWhite);
        auto blueCube  = makeSolidCube(dev, kBlue);

        // Full-screen NDC quad, normal = (0,0,1).
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        auto setupBase = [&](EnvironmentMapEffect& fx) {
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setTextureProperty(&whiteTex);
            fx.setEnvironmentMapProperty(whiteCube.get());
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
        };

        // ── (a) EmissiveColor=red, envAmount=0 → red ─────────────────────
        // litRGB = (1,0,0)*(1,1,1) = (1,0,0); rgb = (1,0,0)*(1,1,1)+0+0 = red.
        {
            dev.Clear(kBlack);
            EnvironmentMapEffect fx(dev);
            setupBase(fx);
            fx.setEmissiveColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kRed), "(a) EmissiveColor=red → red", got, kRed);
        }

        // ── (b) EmissiveColor=green, envAmount=0 → green ─────────────────
        // Same formula; verifies setEmissiveColorProperty setter works.
        {
            dev.Clear(kBlack);
            EnvironmentMapEffect fx(dev);
            setupBase(fx);
            fx.setEmissiveColorProperty(Vector3(0.0f, 1.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kGreen), "(b) EmissiveColor=green → green", got, kGreen);
        }

        // ── (c) EnvironmentMapSpecular=(0,0,1) → blue ────────────────────
        // emissive=0, envAmount=0: litRGB*texColor=0; rgb = 0 + 0 + (0,0,1) = blue.
        {
            dev.Clear(kBlack);
            EnvironmentMapEffect fx(dev);
            setupBase(fx);
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 1.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kBlue), "(c) EnvMapSpecular=blue → blue", got, kBlue);
        }

        // ── (d) EnvironmentMapAmount=1 with blue cube → blue ─────────────
        // emissive=0, envSpecular=0; litRGB*texColor=0;
        // rgb = 0 + blueCube * 1.0 + 0 = (0,0,1) = blue.
        {
            dev.Clear(kBlack);
            EnvironmentMapEffect fx(dev);
            setupBase(fx);
            fx.setEnvironmentMapProperty(blueCube.get());
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kBlue), "(d) EnvMapAmount=1 blue cube → blue", got, kBlue);
        }

        Exit();
    }

public:
    EnvMapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    EnvMapTest game;
    game.Run();
    return game.getResult();
}
