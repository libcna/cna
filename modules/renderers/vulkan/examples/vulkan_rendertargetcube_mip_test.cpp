// SPDX-License-Identifier: MS-PL
// Task 907: verify RenderTargetCube's mip chain is genuinely generated on Vulkan (not just
// present-but-undefined storage), split out of Task 878's original combined RenderTarget2D/
// RenderTargetCube scope.
//
// Direct port of vulkan_rendertargetcube_sample_test.cpp (Task 334) with mipMap=true added to
// the RenderTargetCube constructor -- same 6-face solid-colour-fill + EnvironmentMapEffect-sample
// sequence, proving VulkanRenderTargetCubeRenderer::FaceProxy::MaybeGenerateMips' per-face
// vkCmdBlitImage cascade (mirroring VulkanRenderTargetRenderer::MaybeGenerateMips, Task 878) runs
// without corrupting level 0's actual rendered content or crashing.
//
// Scope decision (deliberate, not an oversight -- tried harder first): unlike RenderTarget2D's
// asymmetric-7:1-split/forced-1x1-destination differential test (Task 878), this test does NOT
// assert on coarser mip *levels'* content specifically. An earlier attempt at exactly that
// (a 7:1 split within one face, backbuffer shrunk to force a coarse LOD via EnvironmentMapEffect's
// reflection-vector derivative) was tried and abandoned: `git stash`-reverting the production fix
// and rebuilding still passed identically, proving it wasn't discriminating -- the same false-
// positive trap Task 878's own 50/50-split draft hit, but this project has no equivalent of
// "shrink the destination rectangle" for a reflection-vector-driven cube sample (its screen-space
// derivative depends on camera/quad geometry this project has no established precise control
// over), and cube-map mip levels never blend across faces, so a uniform-per-face pattern can't
// discriminate at any LOD either. The underlying mip-generation *mechanism* (the vkCmdBlitImage
// cascade itself) is already rigorously verified by Task 878's RenderTarget2D test; this test's
// job is narrower: confirm the identical mechanism, applied per cube face/array layer instead of
// a single 2D image, still produces a valid, correctly-populated level 0 and doesn't crash or
// corrupt anything when mipMap=true. **This narrower goal still caught a real, confirmed bug**
// while writing this test: the per-face blit cascade's own first barrier for each destination
// level assumed it started in SHADER_READ_ONLY_OPTIMAL, which was never true for levels never
// touched by any render pass (only level 0 of an actually-rendered face gets that from its own
// render pass) -- produced live `VUID-vkCmdDraw-None-09600` validation errors (correct pixel
// output regardless, silent otherwise) until fixed with a construction-time full-range initial
// transition (mirrors VulkanRenderTargetRenderer's identical Task 878 fix). Independently
// confirmed via `git stash`: reverting only that specific initial-transition addition reproduces
// the exact validation errors this test's own pixel assertion cannot detect on its own.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCubeSize = 32;

class VulkanRenderTargetCubeMipTest : public Game
{
    std::unique_ptr<RenderTargetCube> rtc_;
    std::unique_ptr<Texture2D>        whiteTex_;
    std::unique_ptr<SpriteBatch>      sb_;
    std::unique_ptr<Texture2D>        blueTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // mipMap=true -- the one difference from Task 334's original sample test.
        rtc_ = std::make_unique<RenderTargetCube>(
                   device, kCubeSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));

        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> blue = { 0, 0, 255, 255 };
        blueTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, blue));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // --- Phase 1: render solid blue into every face (triggers per-face mip regeneration on
        // unbind -- the Task 907 fix). Must be a real draw call, not Clear() alone (Task 875). ---
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            device.SetRenderTarget(rtc_.get(), face);
            sb_->Begin();
            sb_->Draw(*blueTex_,
                      Rectangle(0, 0, kCubeSize, kCubeSize),
                      Rectangle(0, 0, 1, 1),
                      Color::White);
            sb_->End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // --- Phase 2: sample the unbound, now-mip-complete RenderTargetCube via
        // EnvironmentMapEffect ---
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
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() <= 50  &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() >= 200);

        if (pass)
        {
            std::printf("[PASS] VulkanRenderTargetCubeMip: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanRenderTargetCubeMip: centre=(%d,%d,%d), "
                        "expected blue (R<=50, G<=50, B>=200)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanRenderTargetCubeMipTest game;
    game.Run();
    return game.getResult();
}
