// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-026: what `CanBeginDrawEXT()` must answer on Vulkan, measured in
// the state that makes the question non-trivial.
//
// The question
// ------------
// `IGraphicsRenderer::CanBeginDrawEXT()` defaults to `true`; EasyGL overrides it with
// `!metagl::IsContextLost()`, and `GraphicsDeviceManager::BeginDraw` skips Draw and Present for
// the frame when it returns false. Its own comment records why the hook exists at all: FNA
// "returns true whenever a device exists", and the override is there for the browser's
// asynchronous WebGL lost/restored interval. So the default is the XNA-faithful answer, and the
// override is a platform fact EasyGL owns.
//
// Vulkan takes the default. The row asks whether that is right, and names the states worth
// checking -- no swapchain, zero-sized surface, mid-recreation. The reachable one is an
// out-of-date swapchain: `SubmitFrame` calls `RecreateSwapchain()` and returns false, which
// `Present()` ignores.
//
// Why this file exists rather than an argument
// --------------------------------------------
// The state cannot be reached by resizing a window under a virtual display -- it arrives when the
// window manager decides -- so it is injected through `SetSwapchainOutOfDateForTestEXT`, which
// replaces the acquire rather than following it (a handed-over image declared stale would leave
// the image-available semaphore signalled with no waiter, which is a different defect).
//
// Four legs, each of which fails on its own:
//
//   A  CONTROL. An ordinary frame draws a blue quad on green and reads it back. Without this leg
//      every later assertion could be satisfied by a renderer that draws nothing at all.
//   B  The injected frame really was dropped: the acquire counter does not advance across it.
//      Without this the injection could be a no-op and the rest would still pass.
//   C  The answer VULKAN-026 exists to record: `CanBeginDrawEXT()` before, during and after the
//      dropped frame.
//   D  What happens to the frame's recorded draws. `pending3D_` is cleared inside
//      `RecordCommandBuffer`, which a failed acquire never reaches, so the queue survives. This
//      leg measures the retention directly through `GetPendingDrawCountEXT()` rather than
//      inferring it from a colour, and then checks that the renderer still renders correctly
//      afterwards -- recovery, not merely survival.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

class OutOfDateTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D   white_;
    int         frame_   = 0;
    int         failures_ = 0;
    std::uint64_t acquiresBeforeDrop_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

    VulkanRenderer& vk()
    {
        auto* r = dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
        if (r == nullptr) {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            std::abort();
        }
        return *r;
    }

    /// Green background with a quad of @p tint over the middle half.
    void DrawQuad(Color tint)
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(white_, Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1), tint);
        sb_->End();
    }

    Color CentrePixel()
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const Rectangle centre(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<std::uint8_t> px = { 255, 255, 255, 255 };
        white_ = Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        auto& renderer = vk();

        switch (frame_)
        {
        case 0:
            // Warm-up. The first frame under RADV/Wayland can legitimately be out of date on its
            // own, so nothing is judged here.
            DrawQuad(Color(0, 0, 255, 255));
            break;

        case 1:
        {
            // ---- leg A: the control ------------------------------------------------
            DrawQuad(Color(0, 0, 255, 255));
            const Color px = CentrePixel();
            check(px.getBProperty() >= 200 && px.getRProperty() <= 60,
                  "A  an ordinary frame draws and reads back: centre=(" +
                      std::to_string(px.getRProperty()) + "," +
                      std::to_string(px.getGProperty()) + "," +
                      std::to_string(px.getBProperty()) + ") is blue");
            check(renderer.CanBeginDrawEXT(),
                  "C1 CanBeginDrawEXT() is true in the ordinary state");
            break;
        }

        case 2:
            // ---- the dropped frame -------------------------------------------------
            // Record a red quad, then make this frame's acquire report out of date. Present
            // recreates the swapchain and returns without rendering.
            acquiresBeforeDrop_ = renderer.GetAcquireCountEXT();
            DrawQuad(Color(255, 0, 0, 255));
            renderer.SetSwapchainOutOfDateForTestEXT(1);
            break;

        case 3:
        {
            // ---- leg B: the frame really was dropped -------------------------------
            check(renderer.GetAcquireCountEXT() == acquiresBeforeDrop_,
                  "B  the injected frame was dropped: acquire count unchanged at " +
                      std::to_string(renderer.GetAcquireCountEXT()));

            // ---- leg C: the answer this row records --------------------------------
            check(renderer.CanBeginDrawEXT(),
                  "C2 CanBeginDrawEXT() is still true after an out-of-date swapchain -- "
                  "Vulkan recovers inside Present and never asks the caller to skip a frame");

            // ---- leg D: what became of the dropped frame's draws -------------------
            // Both queues, because 2D and 3D work live in separate ones and the first draft of
            // this leg measured only pending3D_ -- which a SpriteBatch quad never enters -- and so
            // reported "nothing retained" for work that was.
            const std::size_t retained3D    = renderer.GetPendingDrawCountEXT();
            const std::size_t retainedBatch = renderer.GetPendingBatchCountEXT();
            check(retained3D + retainedBatch > 0,
                  "D1 the dropped frame's recorded work survives the failed acquire (" +
                      std::to_string(retainedBatch) + " batches, " + std::to_string(retained3D) +
                      " 3D draws) -- both queues are cleared inside RecordCommandBuffer, which "
                      "that path never reaches");

            // And the renderer still works: a fresh frame renders its own content.
            DrawQuad(Color(0, 0, 255, 255));
            const Color px = CentrePixel();
            check(px.getBProperty() >= 200 && px.getRProperty() <= 60,
                  "D2 the next frame renders correctly after recovery: centre=(" +
                      std::to_string(px.getRProperty()) + "," +
                      std::to_string(px.getGProperty()) + "," +
                      std::to_string(px.getBProperty()) + ")");
            check(renderer.GetPendingDrawCountEXT() == 0 &&
                      renderer.GetPendingBatchCountEXT() == 0,
                  "D3 and that submit drained both queues, so the retention does not accumulate");
            break;
        }

        default:
            Exit();
            return;
        }

        ++frame_;
    }

public:
    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    OutOfDateTest game;
    game.Run();
    return game.getResult();
}
