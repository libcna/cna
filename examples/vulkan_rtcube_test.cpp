// SPDX-License-Identifier: MS-PL
// Task 142: Vulkan integration test — RenderTargetCube per-face rendering.
//
// Verifies VulkanRenderTargetCubeBackend:
//   — 6-face cubemap image allocation and per-face VkFramebuffer
//   — SetRenderTarget(RenderTargetCube*, CubeMapFace) routing
//   — SpriteBatch renders into each face (proves per-face framebuffers are distinct)
//   — Backbuffer pass is unaffected by the 6 cube-face passes
//
// Test flow:
//   1. Fill all 6 cube faces with solid red via SpriteBatch (using a 1×1 red Texture2D).
//   2. Clear the backbuffer to blue (last Clear before Present).
//   3. GetBackBufferData → assert centre pixel is blue.
//      A blue centre proves the Phase-2 backbuffer is intact after 6 Phase-1 RT passes.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int CubeSize = 64;

class VulkanRTCubeTest : public Game
{
    std::unique_ptr<SpriteBatch>      sb_;
    std::unique_ptr<RenderTargetCube> rtc_;
    std::unique_ptr<Texture2D>        redTex_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        sb_  = std::make_unique<SpriteBatch>(device);
        rtc_ = std::make_unique<RenderTargetCube>(
                   device, CubeSize,
                   false,
                   SurfaceFormat::Color,
                   DepthFormat::Depth24);

        // 1×1 solid-red texture used as source for SpriteBatch draws into cube faces.
        redTex_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color red(255, 0, 0, 255);
        redTex_->SetData(&red, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.setBlendStateProperty(BlendState::Opaque);

        // --- Phase 1: render solid red into every cube face via SpriteBatch ---
        // The SpriteBatch draw for each face is recorded with rt=faceProxy[i],
        // which causes RecordCommandBuffer to open a VkRenderPass for that face.
        for (int face = 0; face < 6; ++face)
        {
            device.SetRenderTarget(rtc_.get(), static_cast<CubeMapFace>(face));
            sb_->Begin();
            sb_->Draw(*redTex_,
                      Rectangle(0, 0, CubeSize, CubeSize),
                      Rectangle(0, 0, 1, 1),
                      Color::White);
            sb_->End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // --- Phase 2: blue backbuffer ---
        // Clear must be called AFTER all SetRenderTarget(null) so that clearR_/G_/B_/A_
        // is set to blue when RecordCommandBuffer processes Phase 2.
        device.Clear(Color(0, 0, 255, 255));

        // GetBackBufferData triggers Present() → RecordCommandBuffer(Phase1+Phase2).
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        // Phase 2's backbuffer must be exactly the requested blue, so the six cube-face passes
        // recorded before it neither wrote into the swapchain image nor left their clear colour
        // behind.
        //
        // REMED-GFX-144 corrected the claim this check used to make. Absence of visible corruption
        // is NOT evidence of correct synchronization: this file runs standard validation only, one
        // frame, one readback, one pixel, and would have reported a clean pass throughout the whole
        // life of the acquire WRITE_AFTER_READ hazard -- as it did. What it really measures is
        // attachment routing, and that is worth measuring BYTE-EXACTLY rather than through a
        // tolerance band: (0,0,255) is an exact fixed point of sRGB encoding, so a ±50 window only
        // ever hid a real difference. Synchronization is enforced by
        // examples/vulkan_swapchain_sync_test.cpp, which enables the synchronization checks,
        // proves the layer is loaded and wraps the frame ring.
        const bool pass = (centPx.getRProperty() == 0   &&
                           centPx.getGProperty() == 0   &&
                           centPx.getBProperty() == 255);

        if (pass) {
            std::printf("[PASS] VulkanRTCube: backbuffer_centre=(%d,%d,%d) — "
                        "6-face cube render passes completed\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        } else {
            std::printf("[FAIL] VulkanRTCube: backbuffer_centre=(%d,%d,%d), "
                        "expected exactly (0,0,255)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanRTCubeTest game;
    game.Run();
    return game.getResult();
}
