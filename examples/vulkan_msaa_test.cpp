// SPDX-License-Identifier: MS-PL
// Task 147/902: Vulkan backbuffer MSAA integration test.
//
// Original Task 147 version rendered a solid-fill red quad and only checked the centre pixel —
// a methodology that cannot distinguish "MSAA genuinely happened" from "MSAA was silently
// ignored" (both produce an identical solid-red result). Task 902's investigation confirmed this
// was a real false positive: GraphicsDeviceManager.PreferMultiSampling never actually reached the
// Vulkan renderer at all (GraphicsDeviceManager::applyToExistingRenderer() never called
// GraphicsDevice::Reset(), so a preference change after the renderer's initial construction was
// silently dropped). This test now does two things Task 147 didn't:
//
//   1. Uses the diagonal-edge differential methodology already established for RenderTarget2D/
//      RenderTargetCube MSAA (easygl_rendertarget2d_msaa_test.cpp, Task 337; vulkan/bgfx ports,
//      Task 878/879/903): render a diagonal-edged triangle, read back a centre row, and check for
//      a hard binary red/black transition (no AA) vs. genuinely blended intermediate pixels (real
//      AA) — a signature only a true multisample resolve can produce.
//   2. Exercises the REAL, now-fixed runtime path: GraphicsDeviceManager.PreferMultiSampling is
//      toggled and applied via ApplyChanges() *after* the renderer already exists (not just before
//      first construction), which now flows through GraphicsDevice::Reset() ->
//      IGraphicsRenderer::ApplyMultiSampleCount() -> VulkanRenderer's real in-place
//      swapchain/render-pass/pipeline reconfiguration (Task 902). Earlier sibling tests
//      (vulkan_rendertarget2d_msaa_test.cpp etc.) had to work around the gap this task fixes via
//      the NOXNA-only GraphicsDevice::RecreateRendererForMultiSampleCount() escape hatch; this test
//      deliberately does NOT use that hook, to prove the real GraphicsDeviceManager path works.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    // Renders a diagonal-edged triangle directly to the backbuffer (at whatever MultiSampleCount
    // is currently engaged) and returns the full centre-row pixel colours.
    std::vector<Color> RenderAndReadRow()
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color(0, 0, 0, 255));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            { Vector3(-1.0f,  1.0f, 0.0f), white },
            { Vector3( 1.0f,  1.0f, 0.0f), white },
            { Vector3(-1.0f, -1.0f, 0.0f), white },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);

        std::vector<Color> row(W, Color(0, 0, 0, 0));
        const Rectangle reg(0, H / 2, W, 1);
        device.GetBackBufferData(&reg, row.data(), 0, W);
        return row;
    }

    static bool IsBinary(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    static bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return true;
        }
        return false;
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

        // Baseline: preferMultiSampling defaults to false, so the renderer has no MSAA engaged.
        const std::vector<Color> noMsaaRow = RenderAndReadRow();

        // Task 902: toggle the preference and re-apply on the ALREADY-CONSTRUCTED renderer --
        // this is the exact real GraphicsDeviceManager path (not the RecreateRendererForMultiSampleCount
        // NOXNA test-only escape hatch other MSAA tests in this family had to use) that was
        // previously a silent no-op and now genuinely reconfigures the Vulkan swapchain/render
        // pass/pipeline in place.
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->ApplyChanges();
        const std::vector<Color> msaaRow = RenderAndReadRow();

        const bool noMsaaOk = IsBinary(noMsaaRow);
        const bool msaaOk   = HasIntermediate(msaaRow);

        std::printf("[%s] MultiSampleCount=0: diagonal edge is a hard binary transition (no AA)\n",
                    noMsaaOk ? "PASS" : "FAIL");
        std::printf("[%s] MultiSampleCount=8 (applied via GraphicsDeviceManager.ApplyChanges() "
                    "after construction): diagonal edge has genuinely blended pixels (real AA)\n",
                    msaaOk ? "PASS" : "FAIL");

        if (!noMsaaOk)
        {
            std::printf("[INFO] MultiSampleCount=0 row unexpectedly has intermediate pixels — "
                        "either a rasterizer quirk or a false positive in the differential test.\n");
        }
        if (!msaaOk)
        {
            std::printf("[INFO] MultiSampleCount=8 row is purely binary — either "
                        "GraphicsDeviceManager.ApplyChanges() is not really reaching "
                        "VulkanRenderer::ApplyMultiSampleCount(), or the device doesn't "
                        "support 8x backbuffer multisampling.\n");
        }

        result_ = (noMsaaOk && msaaOk) ? 0 : 1;
        Exit();
    }

public:
    VulkanMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->ApplyChanges();
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanMsaaTest game;
    game.Run();
    return game.getResult();
}
