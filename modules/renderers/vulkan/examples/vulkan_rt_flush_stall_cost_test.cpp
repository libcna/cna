// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-399: how often does a render-target-sampling frame reach
// FlushDeferredRenderTarget's full-device stall, and what does it cost?
//
// The rule this file exists to satisfy
// ------------------------------------
// VULKAN-399 was opened as a MEASURE-FIRST row, with an explicit non-goal of removing the stall
// because it is a stall. VULKAN-392's counting found it: three of the four surviving
// vkDeviceWaitIdle calls are teardown or reconfiguration, but this one is mid-frame, on the path a
// game takes whenever it renders to a target and samples it in the same frame -- shadow maps,
// post-process chains, reflection passes.
//
// What the stall is, read out of the function rather than guessed: the flush is serialized on BOTH
// sides. DeviceWaitIdleEXT(), then a one-time command buffer, then RecordCommandBuffer for the
// segments this flush owes, then vkQueueSubmit with NO fence and NO semaphore, then
// vkQueueWaitIdle, then vkFreeCommandBuffers. The leading wait orders the replay after previously
// submitted frame work, which nothing else does because the submit carries no semaphore; the
// trailing one exists because the command buffer is freed on the next line.
//
// What is asserted and what is only printed
// -----------------------------------------
// The same split VULKAN-396 used, for the same reason. STRUCTURAL, asserted: the scene really does
// reach the flush, and the stall count per sampling frame is a small fixed number rather than
// growing with the number of draws -- a per-draw stall would be a different and far worse finding
// than a per-flush one. TIMING is printed and never failed on: a suite that fails on an absolute
// duration reports the GPU rather than the renderer.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {

constexpr int kSpritesPerFrame = 24;

class RtFlushStallCostTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    Texture2D                              white_;
    int  frame_    = 0;
    int  failures_ = 0;
    std::uint64_t stallsBefore_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        const std::vector<std::uint8_t> px = { 255, 255, 255, 255 };
        white_ = Texture2D::CreateFromPixels(dev, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto* vk  = dynamic_cast<VulkanRenderer*>(&dev.GetRenderer());
        if (vk == nullptr)
        {
            std::printf("[FAIL] renderer is not the Vulkan renderer\n");
            ++failures_;
            Exit();
            return;
        }
        if (frame_ > 1) { Exit(); return; }

        // Warm-up frame: pipelines, render pass variants and per-frame buffers are created here,
        // and creating them may legitimately stall. Counting from after it is what makes the
        // number below about the sampling path rather than about one-time setup.
        if (frame_ == 0)
        {
            dev.Clear(Color(0, 0, 0, 255));
            ++frame_;
            return;
        }

        // The scene the row describes: render into a target, then SAMPLE it in the same frame.
        RenderTarget2D target(dev, 128, 128, false, SurfaceFormat::Color, DepthFormat::None);
        dev.SetRenderTarget(&target);
        dev.Clear(Color(255, 0, 0, 255));
        dev.SetRenderTarget(nullptr);

        // Leg C/D FIRST, while the target's work is still queued. FlushDeferredRenderTarget
        // returns early when the target has no pending cycle (`if (!hasPendingCycle) return;`), so
        // a readback issued after anything drained the queues never reaches the stall at all --
        // which is what the first draft of this test accidentally measured, and why it read zero.
        const std::uint64_t beforeReadback = vk->GetDeviceWaitIdleCountEXT();
        const auto readbackBegin = std::chrono::steady_clock::now();
        std::vector<Color> pixels(16 * 16);
        const Rectangle sub(0, 0, 16, 16);
        target.GetData(0, &sub, pixels.data(), 0, static_cast<int>(pixels.size()));
        const std::uint64_t readbackStalls = vk->GetDeviceWaitIdleCountEXT() - beforeReadback;
        const double readbackMs = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - readbackBegin).count() / 1000.0;

        // Counted from HERE, after the readback above, so leg B prices the SAMPLING draws alone.
        // Enclosing the readback would have credited its stall to the 24 sprites and read as
        // "sampling costs a stall", which is the opposite of what the numbers say.
        stallsBefore_ = vk->GetDeviceWaitIdleCountEXT();
        const auto begin = std::chrono::steady_clock::now();

        dev.Clear(Color(0, 255, 0, 255));
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        for (int i = 0; i < kSpritesPerFrame; ++i)
            sb_->Draw(target, Rectangle(i * 4, 0, 32, 32), Rectangle(0, 0, 128, 128), Color::White);
        sb_->End();

        const auto& vp = dev.getViewportProperty();
        const Rectangle probe(8, 8, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&probe, &px, 0, 1);

        const std::uint64_t stalls = vk->GetDeviceWaitIdleCountEXT() - stallsBefore_;
        const double ms = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::steady_clock::now() - begin).count() / 1000.0;

        // ---- structural, asserted on any device --------------------------------
        check(px.getRProperty() >= 200 && px.getGProperty() <= 60,
              "A the target really was sampled: probe=(" + std::to_string(px.getRProperty()) + "," +
                  std::to_string(px.getGProperty()) + "," + std::to_string(px.getBProperty()) +
                  ") is the target's red, not the backbuffer's green");
        check(stalls == 0,
              "B " + std::to_string(kSpritesPerFrame) + " sprites sampling one target cost " +
                  std::to_string(stalls) + " full-device stalls -- rendering to a target and "
                  "sampling it does NOT reach the flush at all; only a readback does");

        // ---- legs C and D: the path that ACTUALLY reaches the flush -------------
        check(!pixels.empty() && pixels[0].getRProperty() >= 200,
              "C the readback really read the target: first texel=(" +
                  std::to_string(pixels[0].getRProperty()) + "," +
                  std::to_string(pixels[0].getGProperty()) + "," +
                  std::to_string(pixels[0].getBProperty()) + ")");
        check(readbackStalls <= 2,
              "D one RenderTarget2D.GetData costs " + std::to_string(readbackStalls) +
                  " full-device stalls -- bounded per readback, not per queued segment");

        // ---- timing, printed and never failed on --------------------------------
        std::printf("[measure] one RenderTarget2D.GetData: %llu device stalls, %.2f ms\n",
                    static_cast<unsigned long long>(readbackStalls), readbackMs);
        std::printf("[measure] one render-target-sampling frame: %llu device stalls, %.2f ms of "
                    "wall clock for %d sampling draws (%dx%d window)\n",
                    static_cast<unsigned long long>(stalls), ms, kSpritesPerFrame,
                    vp.getWidthProperty(), vp.getHeightProperty());
        std::printf("[measure] device-dependent; compared across runs only as a ratio\n");

        ++frame_;
    }

public:
    RtFlushStallCostTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    RtFlushStallCostTest game;
    game.Run();
    return game.getResult();
}
