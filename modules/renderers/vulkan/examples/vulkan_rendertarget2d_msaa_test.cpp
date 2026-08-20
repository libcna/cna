// SPDX-License-Identifier: MS-PL
// Task 878/879: verify MSAA render target creation and resolve behavior on Vulkan.
//
// Direct port of examples/easygl_rendertarget2d_msaa_test.cpp (Task 337) — same methodology, same
// kRTSize, same assertions. See that file's header comment for the full rationale; summarized
// here: a solid-fill test can't distinguish "MSAA resolve doesn't corrupt solid colors" from
// "anti-aliasing genuinely happened" (a non-MSAA target passes a solid-fill test just as
// trivially). This test proves REAL anti-aliasing: render a diagonal-edged triangle into a
// RenderTarget2D, resolve it, sample it back, and check for partially-covered (blended) pixels
// along the diagonal edge — a signature that can only appear if the multisampled resolve actually
// averaged sub-pixel coverage.
//
// Vulkan-specific scaffolding requirement (see plans/plan_graphics.md Task 878/879's Vulkan scope
// decision): per-RT MSAA on this renderer piggybacks on the Vulkan renderer's own already-picked
// sampleCount_ (VulkanRenderer::sampleCount_, picked once at renderer-construction time
// from PresentationParameters.MultiSampleCount). That only becomes > 1 when the game requested
// backbuffer multisampling BEFORE the renderer was constructed.
//
// Verification uncovered a real, separate, pre-existing bug while wiring this test up:
// GraphicsDeviceManager.PreferMultiSampling/ApplyChanges() does NOT actually reach the Vulkan
// renderer at all -- Game's own GraphicsDevice member is unconditionally default-constructed
// (MultiSampleCount=0) in Game::Game()'s member-initializer list, before any derived Game
// subclass (or GraphicsDeviceManager preference-setting) can run, and GraphicsDeviceManager's
// existing apply path only calls GraphicsDevice::SetPresentationParameters(), which deliberately
// does NOT trigger a full device reset (see that method's own doc comment) -- real device
// reset/recreation is a separate, not-yet-implemented FNA feature. This means
// vulkan_msaa_test.cpp (Task 147) has never actually exercised real backbuffer MSAA: its
// solid-red-quad assertion passes identically whether or not MSAA is active, so the gap was
// invisible until this test's differential (binary-vs-blended) methodology could not pass no
// matter what GraphicsDeviceManager preference was requested. Confirmed directly: temporarily
// forcing VulkanRenderer::sampleCount_ > 1 makes this test pass cleanly (both checks,
// zero Vulkan validation errors) on the first attempt, proving the actual Task 878/879 RT-MSAA
// implementation is correct once real backbuffer MSAA is engaged.
//
// Fixing GraphicsDeviceManager's device-reset plumbing generally is out of scope here (a
// separate, large, not-yet-scoped architectural task, mirroring Task 896's precedent for a
// similarly deep pre-existing gap) -- so this test instead uses a new, narrow, CNAEXT test-only
// hook, GraphicsDevice::RecreateRendererForMultiSampleCount(), added specifically to unblock this
// verification: it tears down and rebuilds the renderer (same window) with the requested
// MultiSampleCount, called from Initialize() before any GPU resources exist yet.
//
// No RasterizerState::CullNone override needed — confirmed empirically in every other Vulkan
// pixel test in this family that Vulkan's default cull state already behaves like EasyGL's
// effectively-no-culling default for this test family's standard triangle winding.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 32;

class RenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    bool done_   = false;
    int  result_ = 1;

    // Renders the diagonal triangle into a RenderTarget2D with the given MultiSampleCount, then
    // samples it back onto the backbuffer 1:1, and returns the full centre-row pixel colours.
    std::vector<Color> RenderAndReadRow(GraphicsDevice& device, int multiSampleCount)
    {
        RenderTarget2D rt(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                           DepthFormat::None, multiSampleCount, RenderTargetUsage::DiscardContents);

        device.setBlendStateProperty(BlendState::Opaque);

        device.SetRenderTarget(&rt);
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

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        device.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(rt,
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        std::vector<Color> row(kRTSize, Color(0, 0, 0, 0));
        const Rectangle reg(0, kRTSize / 2, kRTSize, 1);
        device.GetBackBufferData(&reg, row.data(), 0, kRTSize);
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
        auto& device = getGraphicsDeviceProperty();
        // See this file's header comment: GraphicsDeviceManager.PreferMultiSampling never
        // actually reaches the Vulkan renderer, so force it directly here, before any GPU
        // resources (including the SpriteBatch created right below) exist.
        device.RecreateRendererForMultiSampleCount(8);
        sb_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const std::vector<Color> noMsaaRow = RenderAndReadRow(device, 0);
        const std::vector<Color> msaaRow   = RenderAndReadRow(device, 8);

        const bool noMsaaOk = IsBinary(noMsaaRow);
        const bool msaaOk   = HasIntermediate(msaaRow);

        std::printf("[%s] MultiSampleCount=0: diagonal edge is a hard binary transition (no AA)\n",
                    noMsaaOk ? "PASS" : "FAIL");
        std::printf("[%s] MultiSampleCount=8: diagonal edge has genuinely blended pixels (real AA)\n",
                    msaaOk ? "PASS" : "FAIL");

        if (!noMsaaOk)
        {
            std::printf("[INFO] MultiSampleCount=0 row unexpectedly has intermediate pixels — "
                        "either a rasterizer quirk or a false positive in the differential test.\n");
        }
        if (!msaaOk)
        {
            std::printf("[INFO] MultiSampleCount=8 row is purely binary — MSAA resolve is not "
                        "actually averaging sub-pixel coverage (or the device doesn't support "
                        "8x backbuffer multisampling).\n");
        }

        result_ = (noMsaaOk && msaaOk) ? 0 : 1;
        Exit();
    }

public:
    RenderTarget2DMsaaTest()
    {
        // Only used for backbuffer size here -- see this file's header comment for why actually
        // engaging backbuffer MSAA needs the RecreateRendererForMultiSampleCount() call in
        // Initialize() instead of GraphicsDeviceManager.PreferMultiSampling.
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return result_; }
};

int main()
{
    RenderTarget2DMsaaTest game;
    game.Run();
    return game.getResult();
}
