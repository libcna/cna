// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-398/VULKAN-269 -- the "still correct after repeated use" clause of §6.8,
// made testable and then corrected to submit every claimed frame.
//
// Every other test in this suite draws a frame or two and asserts what it drew. That measures a
// renderer's first frame, and a deferred renderer with per-frame caches, retirement queues and a
// swapchain that can be rebuilt under it can be right on frame one and wrong on frame ninety. This
// test runs **120 frames** of everything at once -- a SpriteBatch cycle, a render target bound and
// unbound, a texture uploaded, a vertex buffer rewritten, a per-frame texture created and
// destroyed, and a window resize every 40 frames -- and asserts two different kinds of thing:
//
//   A  The LAST frame is still correct: the render target holds what this frame drew into it, and
//      the backbuffer holds this frame's sprite colour. Not the first frame -- the last.
//   B  Nothing grew without bound. The renderer's own counters are sampled after a warm-up (frame
//      40, past the first resize) and again at the end, and the caches must be IDENTICAL: the
//      pipeline cache, the sampler cache, the sampled-descriptor-set cache and the descriptor
//      pool count. Eighty more frames of the same shapes must not add an entry to any of them.
//      The per-frame texture is the discriminating half of B: it is created, drawn and destroyed
//      every frame, so the (view, sampler) descriptor cache would grow by one per frame if the
//      eviction `VULKAN-213` added did not run.
//   C  A readback builds what it needs once. Both of B's samples are taken after the same
//      backbuffer-readback submission, so neither side gets a one-time cache cost the other lacks.
//      Two later render-target readbacks in a row must also leave the counters unchanged.
//   D  The deferred queues drain: no pending batch and no pending 3D draw survives the last
//      present. A queue that grows by one record per frame is the other shape of the same defect.
//   E  All 120 frame submissions and the resizes really happened, counted through renderer
//      counters, so B and D cannot pass while the test silently queues several iterations as one.
//   F  No validation message across the whole run.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
constexpr int kFrames     = 120;
constexpr int kWarmUp     = 40;   // counters sampled here, past the first resize
constexpr int kResizeEvery = 40;
constexpr int kRT         = 8;
const Color kRTFill(0, 0, 255, 255);
}  // namespace

class VulkanRepeatedUseStressTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    struct Counters
    {
        std::size_t pipelines = 0, samplers = 0, descriptorSets = 0, pools = 0;
        std::string Text() const
        {
            return "pipelines=" + std::to_string(pipelines) + " samplers=" +
                   std::to_string(samplers) + " descSets=" + std::to_string(descriptorSets) +
                   " pools=" + std::to_string(pools);
        }
        bool operator==(const Counters&) const = default;
    };

    Counters Sample()
    {
        auto& r = Renderer();
        return { r.GetGraphicsPipelineCacheEntryCountEXT(), r.GetSamplerCacheSizeEXT(),
                 r.GetSampledDescriptorSetCacheSizeEXT(), r.GetTexSamplerDescriptorPoolCountEXT() };
    }

    static std::unique_ptr<Texture2D> Solid(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        const std::array<std::uint8_t, 16> px{
            c.getRProperty(), c.getGProperty(), c.getBProperty(), 255,
            c.getRProperty(), c.getGProperty(), c.getBProperty(), 255,
            c.getRProperty(), c.getGProperty(), c.getBProperty(), 255,
            c.getRProperty(), c.getGProperty(), c.getBProperty(), 255};
        t->SetDataRGBA(px.data(), 4);
        return t;
    }

    static bool Is(const Color& got, const Color& want)
    {
        return got.getRProperty() == want.getRProperty() &&
               got.getGProperty() == want.getGProperty() &&
               got.getBProperty() == want.getBProperty();
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();
        const uint64_t recreatesBefore = Renderer().GetSwapchainRecreateCountEXT();
        const uint64_t submitsBefore = Renderer().GetFrameSubmitCountEXT();

        // Long-lived resources, created once: what churns is deliberately the per-frame ones.
        RenderTarget2D rt(dev, kRT, kRT, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                          0, RenderTargetUsage::DiscardContents);
        auto sprite = Solid(dev, Color(255, 255, 255, 255));
        VertexBuffer vb(dev, 3);

        Counters warm, endOfLoop;
        Color lastRT(0, 0, 0, 0);
        Color lastBackbuffer(0, 0, 0, 0);

        for (int frame = 0; frame < kFrames; ++frame)
        {
            // The game loop called BeginDraw() for frame zero. Re-open the manager's frame gate
            // after every manual EndDraw below; calling Game::EndDraw repeatedly without this is
            // a no-op after the first call because GraphicsDeviceManager clears drawBegun_.
            if (frame > 0 && !gdm_->BeginDraw())
                throw std::runtime_error("Vulkan repeated-use stress could not begin a frame");

            // The frame's own colour, so a stale frame is visible as the wrong one.
            const std::uint8_t tint = static_cast<std::uint8_t>(40 + (frame % 8) * 20);
            const Color frameColour(tint, tint, tint, static_cast<std::uint8_t>(255));

            // (1) a texture uploaded every frame, into the same object
            {
                const std::array<std::uint8_t, 16> px{
                    frameColour.getRProperty(), frameColour.getGProperty(),
                    frameColour.getBProperty(), 255,
                    frameColour.getRProperty(), frameColour.getGProperty(),
                    frameColour.getBProperty(), 255,
                    frameColour.getRProperty(), frameColour.getGProperty(),
                    frameColour.getBProperty(), 255,
                    frameColour.getRProperty(), frameColour.getGProperty(),
                    frameColour.getBProperty(), 255};
                sprite->SetDataRGBA(px.data(), 4);
            }

            // (2) a render target bound, cleared, drawn into and unbound
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetRenderTarget(&rt);
            dev.Clear(kRTFill);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            // (3) a buffer rewritten and drawn, so the 3D path churns too
            {
                const float z = 0.5f;
                const std::array<float, 12> verts{
                    -0.9f + 0.001f * frame,  0.9f, z, 0.0f,
                    -0.9f,                  -0.9f, z, 0.0f,
                    -0.5f,                  -0.9f, z, 0.0f };
                std::array<std::uint8_t, 48> raw{};
                for (int v = 0; v < 3; ++v) {
                    std::memcpy(raw.data() + v * 16, &verts[v * 4], 12);
                    raw[v * 16 + 12] = frameColour.getRProperty();
                    raw[v * 16 + 13] = frameColour.getGProperty();
                    raw[v * 16 + 14] = frameColour.getBProperty();
                    raw[v * 16 + 15] = 255;
                }
                vb.SetDataRaw(raw.data(), 3, 16);
                BasicEffect fx(dev);
                fx.VertexColorEnabled = true;
                fx.setLightingEnabledProperty(false);
                fx.setTextureEnabledProperty(false);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                dev.SetVertexBuffer(nullptr);
            }

            // (4) a sprite batch drawing this frame's texture and a per-frame texture that dies
            //     at the end of the iteration -- the descriptor cache must not keep its view.
            {
                auto ephemeral = Solid(dev, frameColour);
                SamplerState point = SamplerState::PointClamp;
                SpriteBatch sb(dev);
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                         nullptr, nullptr);
                sb.Draw(*sprite, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 2, 2),
                        Color(255, 255, 255, 255));
                sb.Draw(*ephemeral, Rectangle(16, 0, 16, 16), Rectangle(0, 0, 2, 2),
                        Color(255, 255, 255, 255));
                sb.End();
            }

            if (frame > 0 && frame % kResizeEvery == 0) {
                const int w = 64 + (frame / kResizeEvery) * 16;
                gdm_->setPreferredBackBufferWidthProperty(w);
                gdm_->setPreferredBackBufferHeightProperty(64);
                gdm_->ApplyChanges();
            }

            if (frame == kWarmUp || frame == kFrames - 1)
            {
                // ReadBackbuffer submits and presents the pending frame itself. Use it at both
                // cache-sample points, and retain the last result. Doing the final read after
                // EndDraw would submit a new empty frame and measure that frame's clear instead.
                const Rectangle probe(4, 4, 1, 1);
                Color sampled(0, 0, 0, 0);
                dev.GetBackBufferData(&probe, &sampled, 0, 1);
                if (frame == kFrames - 1) lastBackbuffer = sampled;
            }
            else
            {
                gdm_->EndDraw();
            }

            // Both samples are taken at the SAME point in the frame cycle -- after the frame's
            // backbuffer readback has submitted and presented it. That equality is important:
            // charging a one-time readback pipeline to only one side looks like cache growth.
            if (frame == kWarmUp)      warm      = Sample();
            if (frame == kFrames - 1)  endOfLoop = Sample();
        }

        {
            std::vector<Color> rtPixels(static_cast<std::size_t>(kRT * kRT), Color(0, 0, 0, 0));
            rt.GetData(rtPixels.data(), 0, kRT * kRT);
            lastRT = rtPixels[kRT * kRT / 2];
        }
        const Counters afterFirstReadback = Sample();
        {
            std::vector<Color> rtPixels(static_cast<std::size_t>(kRT * kRT), Color(0, 0, 0, 0));
            rt.GetData(rtPixels.data(), 0, kRT * kRT);
        }
        const Counters afterSecondReadback = Sample();

        check(Is(lastRT, kRTFill),
              "A the render target still holds this frame's clear on frame " +
                  std::to_string(kFrames - 1) + ": " + Text(lastRT) + " (want " + Text(kRTFill) +
                  ")");
        const std::uint8_t lastTint =
            static_cast<std::uint8_t>(40 + ((kFrames - 1) % 8) * 20);
        check(Is(lastBackbuffer,
                 Color(lastTint, lastTint, lastTint, static_cast<std::uint8_t>(255))),
              "A the backbuffer holds the LAST frame's sprite colour, not an earlier one: " +
                  Text(lastBackbuffer) + " (want " +
                  Text(Color(lastTint, lastTint, lastTint,
                             static_cast<std::uint8_t>(255))) + ")");
        check(warm == endOfLoop,
              "B eighty more frames of the same shapes add no cache entry: warm[" + warm.Text() +
                  "] end[" + endOfLoop.Text() + "]");
        check(afterFirstReadback == afterSecondReadback,
              "C a readback builds what it needs ONCE, not per call: first[" +
                  afterFirstReadback.Text() + "] second[" + afterSecondReadback.Text() +
                  "] (the loop ended at [" + endOfLoop.Text() +
                  "]; a readback may add to that, and must then stop)");
        check(Renderer().GetPendingDrawCountEXT() == 0 &&
                  Renderer().GetPendingBatchCountEXT() == 0,
              "D the deferred queues drained: draws=" +
                  std::to_string(Renderer().GetPendingDrawCountEXT()) + " batches=" +
                  std::to_string(Renderer().GetPendingBatchCountEXT()));
        const uint64_t recreates = Renderer().GetSwapchainRecreateCountEXT() - recreatesBefore;
        const uint64_t submits = Renderer().GetFrameSubmitCountEXT() - submitsBefore;
        check(submits == kFrames && recreates >= 2,
              "E all frame cycles submitted and the resizes happened: submits=" +
                  std::to_string(submits) + " recreates=" + std::to_string(recreates) +
                  " (want submits=" + std::to_string(kFrames) + ", recreates>=2)");
        const std::size_t messagesAfter = Renderer().GetValidationMessagesEXT().size();
        check(!VulkanRenderer::IsValidationActiveEXT() || messagesAfter == messagesBefore,
              "F no validation message across " + std::to_string(kFrames) + " frames: " +
                  std::to_string(messagesBefore) + " -> " + std::to_string(messagesAfter));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanRepeatedUseStressTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanRepeatedUseStressTest game;
    game.Run();
    return game.getResult();
}
