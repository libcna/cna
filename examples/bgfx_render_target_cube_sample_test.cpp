// SPDX-License-Identifier: MS-PL
// Task 334: RenderTargetCube sampled as TextureCube after unbinding, via EnvironmentMapEffect,
// on Bgfx.
//
// This mirrors Task 333's bgfx_render_target_sample_test.cpp finding (Task 873): confirmed by
// direct layout analysis that BgfxGraphicsBackend's EnvironmentMapEffect path has the exact same
// unsafe-cast bug, one level over. `static_cast<const BgfxTextureCubeBackend&>(*params.envMap)`
// (in DrawPrimitivesEx's envMapping branch) assumes params.envMap is always a
// BgfxTextureCubeBackend, but a RenderTargetCube's backend is a BgfxRenderTargetCubeBackend —
// an unrelated sibling class (inherits only IRenderTargetCubeBackend/ITextureCubeBackend).
// BgfxTextureCubeBackend's first data member is `bgfx::TextureHandle handle`;
// BgfxRenderTargetCubeBackend's first data member is `bgfx::FrameBufferHandle fbo` (its actual
// colour texture, `cubeTex`, is the SECOND member) — both handle structs are
// `struct { uint16_t idx; }` (see bgfx.h's BGFX_HANDLE macro), so the cast compiles and reads
// `fbo` where `handle` should be: a framebuffer-pool handle passed where a texture-pool handle
// is expected. Same bug shape and severity as Task 873, tracked as Task 874, not fixed here.
//
// Exit code 0 = did not crash (no pixel-correctness assertion is possible on Bgfx).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

class BgfxRenderTargetCubeSampleTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTargetCube>      rtc_;
    std::unique_ptr<Texture2D>             whiteTex_;
    int frame_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        rtc_ = std::make_unique<RenderTargetCube>(
                   device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
    }

    void Draw(const GameTime&) override
    {
        if (frame_ > 0) return;
        ++frame_;

        auto& device = getGraphicsDeviceProperty();
        // Note: SetDepthTestEnabled/setBlendStateProperty are not exercised here — the Bgfx
        // backend does not yet wire 3D depth/blend state changes (see bgfx_render_target_usage_test.cpp).

        // --- Phase 1: bind/clear each cube face, then unbind ---
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            device.SetRenderTarget(rtc_.get(), face);
            device.Clear(Color(0, 0, 255, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // --- Phase 2: sample the unbound RenderTargetCube via EnvironmentMapEffect ---
        // If BgfxGraphicsBackend's static_cast<BgfxTextureCubeBackend&> on a
        // BgfxRenderTargetCubeBackend is truly unrelated-type UB, this call is where it manifests.
        device.Clear(Color(0, 0, 0, 255));

        EnvironmentMapEffect fx(device);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setTextureProperty(whiteTex_.get());
        fx.setEnvironmentMapProperty(rtc_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone. Bgfx already culls CCW today, but this test only
        // checks for "no crash", not pixel correctness, so the culling was silently invisible.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        std::printf("[PASS] Bgfx RenderTargetCube-as-TextureCube EnvironmentMapEffect draw did not crash\n");
        Exit();
    }

public:
    BgfxRenderTargetCubeSampleTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }
};

int main()
{
    BgfxRenderTargetCubeSampleTest game;
    game.Run();
    return 0;
}
