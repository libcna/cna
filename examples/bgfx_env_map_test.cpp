// SPDX-License-Identifier: MS-PL
// Task 278: Bgfx EnvironmentMapEffect smoke test.
//
// Task 278's audit found that Bgfx had NO code path for GpuDrawParams::envMapping at all —
// DrawPrimitivesEx never checked the flag, so drawing with EnvironmentMapEffect silently fell
// back to the plain lit-textured shader (no reflection, no cube-map sampling, no crash). Fixed by
// adding a dedicated envMap3DProgram_ (vs_env_map3d.sc/fs_env_map3d.sc, mirroring the EasyGL/Vulkan
// reflection formula) and wiring params.envMapping into DrawPrimitivesEx.
//
// Bgfx has no GPU readback API in this project (see bgfx_render_target_usage_test.cpp), so unlike
// easygl_env_map_test.cpp / vulkan_env_map_test.cpp this cannot pixel-verify the reflection colour.
// This is a smoke test: exercise all 4 of the EasyGL/Vulkan test's configurations (varying
// EmissiveColor, EnvironmentMapAmount, EnvironmentMapSpecular, and the cube map itself) across
// several frames and verify no crash / no exception.
//
// Exit code 0 = no crash.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BgfxEnvMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  frame_  = 0;
    bool failed_ = false;

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
        auto& dev = getGraphicsDeviceProperty();

        try
        {
            // Note: SetDepthTestEnabled/setBlendStateProperty are not exercised here — the Bgfx
            // renderer does not yet wire 3D depth/blend state changes (see bgfx_render_target_usage_test.cpp).
            dev.Clear(Color(0, 0, 0, 255));
            // Task 896 finding: the shared quad's winding is CCW/back-facing under CNA's real
            // default RasterizerState — needs CullNone. Bgfx already culls CCW today, but this
            // test only checks for "no crash", not pixel correctness, so it was silently masked.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            const Color kWhite(255, 255, 255, 255);
            const Color kBlue (  0,   0, 255, 255);

            Texture2D whiteTex(dev, 1, 1);
            whiteTex.SetData(&kWhite, 1);
            auto whiteCube = makeSolidCube(dev, kWhite);
            auto blueCube  = makeSolidCube(dev, kBlue);

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

            // (a) EmissiveColor=red, envAmount=0
            {
                EnvironmentMapEffect fx(dev);
                setupBase(fx);
                fx.setEmissiveColorProperty(Vector3(1.0f, 0.0f, 0.0f));
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
            // (b) EmissiveColor=green, envAmount=0
            {
                EnvironmentMapEffect fx(dev);
                setupBase(fx);
                fx.setEmissiveColorProperty(Vector3(0.0f, 1.0f, 0.0f));
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
            // (c) EnvironmentMapSpecular=blue
            {
                EnvironmentMapEffect fx(dev);
                setupBase(fx);
                fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 1.0f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
            // (d) EnvironmentMapAmount=1 with a blue cube map
            {
                EnvironmentMapEffect fx(dev);
                setupBase(fx);
                fx.setEnvironmentMapProperty(blueCube.get());
                fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            }
        }
        catch (const std::exception& e)
        {
            std::printf("[FAIL] Bgfx EnvironmentMapEffect smoke: exception: %s\n", e.what());
            failed_ = true;
            Exit();
            return;
        }

        if (++frame_ >= 3)
        {
            std::printf("[%s] Bgfx EnvironmentMapEffect smoke: %d frames drawn without crash\n",
                        failed_ ? "FAIL" : "PASS", frame_);
            Exit();
        }
    }

public:
    BgfxEnvMapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    bool Failed() const { return failed_; }
};

int main()
{
    BgfxEnvMapTest game;
    game.Run();
    return game.Failed() ? 1 : 0;
}
