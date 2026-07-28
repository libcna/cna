// SPDX-License-Identifier: MS-PL
// Task 334: Vulkan integration test — RenderTargetCube sampled as TextureCube after unbinding,
// via EnvironmentMapEffect.
//
// Mirrors Task 142's vulkan_rtcube_test.cpp (per-face rendering) combined with Task 136's
// vulkan_env_map_test.cpp (EnvironmentMapEffect sampling), but adds the missing piece neither
// covered: actually sampling the RenderTargetCube's rendered content back out as a TextureCube.
//
// Sequence:
//   Step 1 (cube-face bind cycles): SpriteBatch-draw a solid-blue 1x1 texture into each of the 6
//     RenderTargetCube faces.
//   Step 2 (a backbuffer bind cycle): draw a full-screen NDC quad with EnvironmentMapEffect,
//     EnvironmentMapAmount=1, EmissiveColor=0, EnvironmentMapSpecular=0 (isolates the env-map
//     term), sampling the now-unbound RenderTargetCube via setEnvironmentMapProperty.
//   GetBackBufferData triggers Present() -> RecordCommandBuffer records both cycles in public
//   order (REMED-GFX-140/143 replaced its old two-phase "every target pass, then one trailing
//   swapchain pass" shape with one ordered stream of bind cycles); centre
//   pixel must be blue if VulkanRenderTargetCubeBackend's actual rendered cubeView_ (not stale
//   or garbage data) was sampled — confirmed via VulkanRenderTargetCubeBackend's
//   IVulkanCubeSamplable::GetVkCubeImageView() override, reached through a safe dynamic_cast in
//   VulkanGraphicsBackend (unlike Bgfx's unsafe static_cast, see Tasks 873/874).
//
// NOTE: Phase 1 must use a real draw call (SpriteBatch), not GraphicsDevice::Clear() alone.
// An earlier version of this test used Clear()-only per face and failed with a Vulkan
// validation error (every face's image layer stuck at VK_IMAGE_LAYOUT_UNDEFINED instead of
// SHADER_READ_ONLY_OPTIMAL) — VulkanGraphicsBackend::Clear() only records a single global clear
// colour (clearR_/G_/B_/A_) and does not register the currently-bound RT as "used"; only an
// actual draw call adds the RT to RecordCommandBuffer's usedRTs list and gets its render pass
// (and the finalLayout transition to SHADER_READ_ONLY_OPTIMAL) recorded at all. Confirmed this
// is a real, previously-untested gap — tracked as Task 875, not fixed here.
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

class VulkanRenderTargetCubeSampleTest : public Game
{
    std::unique_ptr<RenderTargetCube> rtc_;
    std::unique_ptr<Texture2D>        whiteTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D>   blueTex_;

    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        rtc_ = std::make_unique<RenderTargetCube>(
                   device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);

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

        // --- Phase 1: render solid blue into every face of the RenderTargetCube ---
        // Must be a real draw call, not Clear() alone — see the header comment.
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

        // --- Step 2: sample the unbound RenderTargetCube via EnvironmentMapEffect ---
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

        // GetBackBufferData triggers Present() -> RecordCommandBuffer records both bind cycles,
        // the cube faces' and this backbuffer one, in that public order.
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() <= 50  &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() >= 200);

        if (pass)
        {
            std::printf("[PASS] VulkanRenderTargetCubeSample: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanRenderTargetCubeSample: centre=(%d,%d,%d), "
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
    VulkanRenderTargetCubeSampleTest game;
    game.Run();
    return game.getResult();
}
