// SPDX-License-Identifier: MS-PL
// Task 334: EasyGL integration test — RenderTargetCube sampled as TextureCube after unbinding,
// via EnvironmentMapEffect.
//
// Sequence:
//   1. Render solid blue into all 6 faces of a RenderTargetCube (SetRenderTarget(rt, face),
//      Clear, unbind after each face).
//   2. Draw a full-screen NDC quad with EnvironmentMapEffect, EnvironmentMapAmount=1,
//      EmissiveColor=0, EnvironmentMapSpecular=0 (isolates the env-map term, mirrors
//      easygl_env_map_test.cpp's sub-test (d), but the cube is a RenderTargetCube instead
//      of a plain TextureCube filled via SetData).
//   3. Read back the centre pixel — must be blue if the RenderTargetCube's actual rendered
//      content (not stale/garbage data) was sampled.
//
// Confirms the RenderTargetCube-as-TextureCube path is architecturally sound on EasyGL:
// EnvironmentMapEffect::FillGpuDrawParams reads envMap via TextureCube::GetRenderer() (shared
// by RenderTargetCube), and EasyGLRenderer calls params.envMap->BindGL() through virtual
// dispatch — both EasyGLTextureCubeRenderer and EasyGLRenderTargetCubeRenderer override BindGL()
// correctly (confirmed by code reading), so no renderer-specific cast bug exists here unlike the
// Bgfx SpriteBatch/EnvironmentMapEffect issues found in Tasks 873/874.
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

class RenderTargetCubeSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kBlue(0, 0, 255, 255);
        const Color kWhite(255, 255, 255, 255);

        // --- Phase 1: render solid blue into every face of a RenderTargetCube ---
        RenderTargetCube rtc(dev, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            dev.SetRenderTarget(&rtc, face);
            dev.Clear(kBlue);
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: sample the unbound RenderTargetCube via EnvironmentMapEffect ---
        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        dev.Clear(Color(0, 0, 0, 255));

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
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setTextureProperty(&whiteTex);
        fx.setEnvironmentMapProperty(&rtc);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        Color got = readCenter(dev);
        const bool pass = colourMatch(got, kBlue);
        if (pass)
        {
            std::printf("[PASS] RenderTargetCube sampled via EnvironmentMapEffect: got=(%d,%d,%d)\n",
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] RenderTargetCube sampled via EnvironmentMapEffect: got=(%d,%d,%d), "
                        "expected≈(0,0,255)\n",
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    RenderTargetCubeSampleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    RenderTargetCubeSampleTest game;
    game.Run();
    return game.getResult();
}
