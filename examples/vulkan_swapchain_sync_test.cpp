// SPDX-License-Identifier: MS-PL
// REMED-GFX-144: the acquired swapchain image must not be written before vkAcquireNextImageKHR
// has handed it over.
//
// The defect
// ----------
// `SubmitFrame` waits the acquire semaphore with
//
//     pWaitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
//
// which blocks COLOR_ATTACHMENT_OUTPUT and every LATER stage of every command in the batch, and
// nothing earlier. The first thing the batch does to the acquired image is `vkCmdBeginRenderPass`,
// whose automatic `initialLayout` transition is a WRITE. That transition is not a command stage --
// it is ordered solely by the render pass's VK_SUBPASS_EXTERNAL -> 0 subpass dependency, happening
// after the availability operations of that dependency's first synchronization scope
// (`srcStageMask`) and before the visibility operations of its second (`dstStageMask`).
//
// Every stage in the swapchain passes' `srcStageMask` -- FRAGMENT_SHADER, EARLY_FRAGMENT_TESTS,
// LATE_FRAGMENT_TESTS -- precedes COLOR_ATTACHMENT_OUTPUT in pipeline order, while `dstStageMask`
// demanded the transition complete before EARLY_FRAGMENT_TESTS, which the semaphore wait does not
// block. So the layout transition was free to run BEFORE the acquire semaphore signalled, writing
// an image the presentation engine had not finished reading. The fix adds
// COLOR_ATTACHMENT_OUTPUT to that first scope, which is exactly the stage the wait blocks.
//
// Why this file exists rather than a grep over a log
// --------------------------------------------------
// Three independent things are asserted, because each alone is a false positive waiting to happen:
//
//   1. CLASSIFIED VALIDATION. Synchronization validation is requested IN PROCESS, through
//      `VkValidationFeaturesEXT` (`SetSyncValidationEnabledEXT`), so the check cannot silently
//      degrade because a runner dropped an environment variable or a settings file moved. The
//      layer being live is asserted, not assumed -- `IsValidationActiveEXT()` is false when the
//      Khronos layer was missing, and this file FAILS in that case instead of reporting a
//      meaningless zero. Messages are classified by the stable `SYNC-HAZARD` token and by the
//      participating API names, never by one full diagnostic sentence.
//
//   2. THE FRAME MODEL. Zero hazards is worthless if the run never wrapped a frame slot, never
//      re-entered a swapchain image and never recreated the swapchain. The backend's bounded
//      counters make that measurable: every frame slot used, more than one distinct swapchain
//      image acquired, one acquire / one frame submit / one present per rendered frame, at most
//      one extra CPU fence wait per frame, and no growth at all in semaphores, fences or command
//      buffers across dozens of frames and a resize.
//
//   3. EXACT PIXELS. A renderer that synchronizes perfectly and draws the wrong thing is still
//      broken, so every phase ends in a byte-exact backbuffer readback. The palette is 0/255-only,
//      an exact fixed point of sRGB encoding, and every asserted region is a full-height vertical
//      stripe, so nothing depends on rounding or on readback orientation.
//
// The phases run enough consecutive frames to wrap the frame slots several times, include
// REMED-GFX-143's backbuffer/render-target interleaving so the multi-segment command buffer is the
// one being synchronized, and drive a real swapchain recreation through
// `GraphicsDeviceManager::ApplyChanges()`.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Vulkan::VulkanGraphicsBackend;

namespace
{
    constexpr int kBBW = 64;   ///< Requested backbuffer width.
    constexpr int kBBH = 64;   ///< Requested backbuffer height.
    constexpr int kRT  = 8;    ///< Render-target edge.
    constexpr int kStripes = 4;

    /// Frames rendered with nothing but Present between them, before the first readback.
    constexpr int kPlainFrames = 10;
    /// Frames of REMED-GFX-143 backbuffer/target interleaving.
    constexpr int kInterleavedFrames = 6;
    /// Frames rendered after the resize.
    constexpr int kPostResizeFrames = 6;
    /// REMED-GFX-129: frames spent driving ordered vkCmdClearAttachments commands.
    constexpr int kClearFrames = 6;

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);
    const Color kBlack(0, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /// One classified backbuffer readback.
    struct Frame
    {
        bool threw = false;
        std::string what;
        int w = 0;
        int h = 0;
        std::vector<Color> px;

        [[nodiscard]] Color At(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= w || y >= h) return Color(0, 0, 0, 0);
            return px[static_cast<std::size_t>(y) * w + x];
        }
    };

    /// A snapshot of every bounded synchronization object the backend owns.
    struct Cardinality
    {
        int imageAvailable = 0;
        int renderFinished = 0;
        int fences         = 0;
        int commandBuffers = 0;

        bool operator==(const Cardinality&) const = default;

        [[nodiscard]] std::string Text() const
        {
            return "imageAvailable=" + std::to_string(imageAvailable) +
                   " renderFinished=" + std::to_string(renderFinished) +
                   " fences=" + std::to_string(fences) +
                   " commandBuffers=" + std::to_string(commandBuffers);
        }
    };
}

class VulkanSwapchainSyncTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<RenderTarget2D> rt_;
    // REMED-GFX-129: the surfaces the ordered-clear phase drives. `preserve_` is the shape whose
    // pass loads its colour, so EVERY Clear() into it must be an explicit command; `depth_` owns a
    // real depth+stencil aspect pair; `mrtA_`/`mrtB_` make a clear write more than one attachment;
    // `cube_` makes it write a single-layer face framebuffer.
    std::unique_ptr<RenderTarget2D>   preserve_;
    std::unique_ptr<RenderTarget2D>   depth_;
    std::unique_ptr<RenderTarget2D>   mrtA_;
    std::unique_ptr<RenderTarget2D>   mrtB_;
    std::unique_ptr<RenderTargetCube> cube_;

    int bbW_ = kBBW;
    int bbH_ = kBBH;

    int frame_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    /// Validation messages already accounted for, so each phase judges only what it added.
    std::size_t seenMessages_ = 0;
    /// Counter readings taken at phase boundaries.
    std::uint64_t acquiresAtPlainEnd_ = 0;
    std::uint64_t recreatesBeforeResize_ = 0;
    Cardinality baseline_{};
    int readbacks_ = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount_;
        if (ok) ++passCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    }

    // ---------------------------------------------------------------------
    // Backend access
    // ---------------------------------------------------------------------

    VulkanGraphicsBackend& Backend()
    {
        auto* vk = dynamic_cast<VulkanGraphicsBackend*>(&getGraphicsDeviceProperty().GetBackend());
        // The target is registered only for CNA_GRAPHICS_BACKEND=VULKAN, so this cannot be null.
        return *vk;
    }

    [[nodiscard]] Cardinality Cardinalities()
    {
        auto& b = Backend();
        return Cardinality{ b.GetImageAvailableSemaphoreCountEXT(),
                            b.GetRenderFinishedSemaphoreCountEXT(),
                            b.GetFrameFenceCountEXT(),
                            b.GetFrameCommandBufferCountEXT() };
    }

    /**
     * @brief Classifies every validation message emitted since the previous call.
     *
     * Grouping is by stable tokens -- the `SYNC-HAZARD` message-name prefix and the names of the
     * participating entry points -- not by matching one complete diagnostic sentence, so a layer
     * rewording cannot turn a real hazard into a green run.
     */
    void JudgeNewValidation(const std::string& label)
    {
        const auto& all  = Backend().GetValidationMessagesEXT();
        const auto& ids  = Backend().GetValidationMessageIdNamesEXT();
        int syncHazards = 0;
        int acquireHazards = 0;
        int other = 0;
        std::string firstSync;
        std::string firstOther;
        for (std::size_t i = seenMessages_; i < all.size(); ++i)
        {
            const std::string& m  = all[i];
            const std::string& id = (i < ids.size()) ? ids[i] : std::string();
            // The message-ID NAME is the classifier; the sentence is only ever quoted in the
            // failure text. "hazard detected" is accepted as a fallback so a layer that omits the
            // id name cannot silently reclassify a hazard as something else.
            if (id.find("SYNC-HAZARD") != std::string::npos ||
                m.find("hazard detected") != std::string::npos)
            {
                ++syncHazards;
                if (m.find("vkAcquireNextImageKHR") != std::string::npos) ++acquireHazards;
                if (firstSync.empty()) firstSync = id + " -- " + m.substr(0, 200);
            }
            else
            {
                ++other;
                if (firstOther.empty()) firstOther = id + " -- " + m.substr(0, 200);
            }
        }
        seenMessages_ = all.size();

        check(syncHazards == 0,
              label + ": zero synchronization hazards" +
                  (syncHazards == 0
                       ? ""
                       : " -- got " + std::to_string(syncHazards) + " (" +
                             std::to_string(acquireHazards) + " naming vkAcquireNextImageKHR): " +
                             firstSync));
        check(other == 0,
              label + ": zero other validation messages" +
                  (other == 0 ? "" : " -- got " + std::to_string(other) + ": " + firstOther));
    }

    // ---------------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------------

    [[nodiscard]] int StripeX(int s) const { return (bbW_ * s) / kStripes; }
    [[nodiscard]] int StripeW() const { return bbW_ / kStripes; }

    [[nodiscard]] Rectangle StripeRect(int from, int to) const
    {
        return Rectangle(StripeX(from), 0, StripeX(to) - StripeX(from), bbH_);
    }

    void SpriteStripes(int from, int to, const Color& colour)
    {
        sb_->Begin();
        sb_->Draw(*white_, StripeRect(from, to), Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    void SpriteTargetStripes(Texture2D& src, int from, int to)
    {
        sb_->Begin();
        sb_->Draw(src, StripeRect(from, to), Rectangle(0, 0, kRT, kRT), Color::White);
        sb_->End();
    }

    void FillTarget(const Color& colour)
    {
        sb_->Begin();
        sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    Frame ReadBackbuffer(GraphicsDevice& dev)
    {
        ++readbacks_;
        Frame f;
        f.w = bbW_;
        f.h = bbH_;
        f.px.assign(static_cast<std::size_t>(bbW_) * bbH_, Color(0xCD, 0xCD, 0xCD, 0xCD));
        Rectangle all(0, 0, bbW_, bbH_);
        try
        {
            dev.GetBackBufferData(&all, f.px.data(), 0, static_cast<int>(f.px.size()));
        }
        catch (const std::exception& e) { f.threw = true; f.what = e.what(); }
        catch (...)                     { f.threw = true; f.what = "unknown exception"; }
        return f;
    }

    void CheckStripes(const Frame& f, const std::array<Color, kStripes>& expected,
                      const std::string& label)
    {
        if (f.threw)
        {
            check(false, label + ": GetBackBufferData raised: " + f.what);
            return;
        }
        const int q = StripeW();
        std::string firstBad;
        int bad = 0;
        for (int s = 0; s < kStripes; ++s)
        {
            const std::array<int, 2> xs = { StripeX(s) + q / 4, StripeX(s) + (3 * q) / 4 };
            const std::array<int, 3> ys = { bbH_ / 4, bbH_ / 2, (3 * bbH_) / 4 };
            for (int x : xs)
                for (int y : ys)
                {
                    const Color got = f.At(x, y);
                    if (Same(got, expected[static_cast<std::size_t>(s)])) continue;
                    ++bad;
                    if (firstBad.empty())
                        firstBad = " first at stripe " + std::to_string(s) + " (" +
                                   std::to_string(x) + "," + std::to_string(y) + ") expected " +
                                   ColorText(expected[static_cast<std::size_t>(s)]) + " got " +
                                   ColorText(got);
                }
        }
        check(bad == 0, bad == 0 ? label
                                 : label + ": " + std::to_string(bad) + " probes wrong," + firstBad);
    }

    void SyncViewport(GraphicsDevice& dev)
    {
        const Viewport vp = dev.getViewportProperty();
        if (vp.getWidthProperty() > 0 && vp.getHeightProperty() > 0)
        {
            bbW_ = vp.getWidthProperty();
            bbH_ = vp.getHeightProperty();
        }
    }

    // ---------------------------------------------------------------------
    // Phases
    // ---------------------------------------------------------------------

    /// One ordinary backbuffer frame. Nothing but Present separates it from the next.
    void PlainFrame(GraphicsDevice& dev, int n)
    {
        dev.Clear(kBlack);
        const Color colour = (n % 2 == 0) ? kBlue : kYellow;
        SpriteStripes(0, kStripes, colour);
        (void)dev;
    }

    /// REMED-GFX-143's shape: produce into a target, consume it on the backbuffer, produce again,
    /// consume again -- all inside ONE frame, so the command buffer holds several segments and the
    /// swapchain image is entered more than once.
    void InterleavedFrame(GraphicsDevice& dev)
    {
        dev.Clear(kBlack);

        dev.SetRenderTarget(rt_.get());
        FillTarget(kRed);
        dev.SetRenderTarget(nullptr);
        SpriteTargetStripes(*rt_, 0, 2);

        dev.SetRenderTarget(rt_.get());
        FillTarget(kGreen);
        dev.SetRenderTarget(nullptr);
        SpriteTargetStripes(*rt_, 2, kStripes);
    }

    /**
     * @brief REMED-GFX-129's shape: every route that records a vkCmdClearAttachments command.
     *
     * A clear is now a real command inside an active render pass rather than a load action, which
     * is a new WRITE to a colour or depth/stencil attachment at a point the pass's own external
     * subpass dependencies were never written for. This frame drives all of it in one command
     * buffer: a preserving colour target (LOAD_OP_LOAD, so the clear must be explicit), a clear
     * after a draw, several clears in one cycle, depth-only and stencil-only clears against a real
     * Depth24Stencil8 attachment, an MRT set where one clear writes both attachments, a cube face
     * whose framebuffer is single-layer, and a backbuffer clear issued after backbuffer geometry.
     */
    void OrderedClearFrame(GraphicsDevice& dev)
    {
        dev.Clear(kBlack);

        // Preserving colour target: draw, then clear, then draw again.
        dev.SetRenderTarget(preserve_.get());
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillTarget(kBlue);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        // Real depth+stencil aspects, each cleared on its own and then together.
        dev.SetRenderTarget(depth_.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kRed, 1.0f, 0);
        FillTarget(kGreen);
        dev.Clear(ClearOptions::DepthBuffer, kWhite, 0.5f, 0);
        dev.Clear(ClearOptions::Stencil, kWhite, 1.0f, 7);
        dev.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, kWhite, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        // One clear across two colour attachments.
        dev.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(mrtA_.get())),
                              RenderTargetBinding(static_cast<Texture*>(mrtB_.get()))});
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kBlue, 1.0f, 0);
        dev.SetRenderTargets({});

        // A single-layer cube-face framebuffer.
        dev.SetRenderTarget(cube_.get(), CubeMapFace::NegativeZ);
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTargets({});

        // Backbuffer: geometry first, then the clear, then geometry again -- the shape that used to
        // run the clear before both draws.
        SpriteStripes(0, kStripes, kBlue);
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        SpriteStripes(0, 2, kGreen);
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        rt_ = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0,
                                               RenderTargetUsage::DiscardContents);
        preserve_ = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                     DepthFormat::None, 0,
                                                     RenderTargetUsage::PreserveContents);
        depth_ = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                  DepthFormat::Depth24Stencil8, 0,
                                                  RenderTargetUsage::PreserveContents);
        mrtA_ = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                 DepthFormat::None, 0,
                                                 RenderTargetUsage::PreserveContents);
        mrtB_ = std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                 DepthFormat::None, 0,
                                                 RenderTargetUsage::PreserveContents);
        cube_ = std::make_unique<RenderTargetCube>(dev, kRT, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& backend = Backend();

        // ---- frame 0: warm-up and the liveness assertions -----------------
        if (frame_ == 0)
        {
            SyncViewport(dev);
            std::printf("REMED-GFX-144 Vulkan swapchain synchronization -- backbuffer %dx%d, "
                        "frame slots %d, swapchain images %d\n",
                        bbW_, bbH_, VulkanGraphicsBackend::GetFramesInFlightEXT(),
                        backend.GetSwapchainImageCountEXT());

            // A zero hazard count means nothing if the layer never loaded. Fail loudly instead.
            check(VulkanGraphicsBackend::IsValidationActiveEXT(),
                  "L1 validation layer active: VK_LAYER_KHRONOS_validation is loaded, so a zero "
                  "hazard count is a measurement and not an absence of measurement");
            check(backend.GetSwapchainImageCountEXT() >= 2,
                  "L2 swapchain has at least two images, so an image index can actually vary "
                  "(got " + std::to_string(backend.GetSwapchainImageCountEXT()) + ")");
            baseline_ = Cardinalities();
            check(baseline_ == (Cardinality{ VulkanGraphicsBackend::GetFramesInFlightEXT(),
                                             VulkanGraphicsBackend::GetFramesInFlightEXT(),
                                             VulkanGraphicsBackend::GetFramesInFlightEXT(),
                                             VulkanGraphicsBackend::GetFramesInFlightEXT() }),
                  "L3 one image-available semaphore, one render-finished semaphore, one fence and "
                  "one command buffer per frame slot: " + baseline_.Text());

            dev.Clear(kBlack);
            SpriteStripes(0, kStripes, kBlack);
            ++frame_;
            return;
        }

        // ---- phase A: consecutive ordinary frames -------------------------
        if (frame_ <= kPlainFrames)
        {
            PlainFrame(dev, frame_);
            ++frame_;
            return;
        }

        if (frame_ == kPlainFrames + 1)
        {
            // Judged AFTER the plain run, so the hazard had every chance to appear on an ordinary
            // one-segment frame, on two consecutive frames and on a full wrap of the frame slots.
            const std::uint64_t acquires = backend.GetAcquireCountEXT();
            acquiresAtPlainEnd_ = acquires;
            check(acquires >= static_cast<std::uint64_t>(kPlainFrames),
                  "A1 " + std::to_string(acquires) + " acquires over " +
                      std::to_string(kPlainFrames) + " consecutive plain frames");
            check(backend.GetUsedFrameSlotCountEXT() ==
                      VulkanGraphicsBackend::GetFramesInFlightEXT(),
                  "A2 every frame slot was used, so the run wrapped the in-flight ring (" +
                      std::to_string(backend.GetUsedFrameSlotCountEXT()) + "/" +
                      std::to_string(VulkanGraphicsBackend::GetFramesInFlightEXT()) + ")");
            check(backend.GetDistinctAcquiredImageCountEXT() >= 2,
                  "A3 more than one distinct swapchain image index was acquired, so the acquire "
                  "dependency was exercised against different images (" +
                      std::to_string(backend.GetDistinctAcquiredImageCountEXT()) + " of " +
                      std::to_string(backend.GetSwapchainImageCountEXT()) + ")");
            check(backend.GetPresentCountEXT() == acquires &&
                      backend.GetFrameSubmitCountEXT() == acquires,
                  "A4 exactly one frame submit and one present per acquire -- no extra frame, no "
                  "extra present, no per-segment submission (acquire=" + std::to_string(acquires) +
                      " submit=" + std::to_string(backend.GetFrameSubmitCountEXT()) +
                      " present=" + std::to_string(backend.GetPresentCountEXT()) + ")");
            check(backend.GetFrameFenceWaitCountEXT() <= acquires + 1,
                  "A5 at most one frame-fence wait per acquire, so nothing serializes the renderer "
                  "beyond the one wait the frame ring needs (waits=" +
                      std::to_string(backend.GetFrameFenceWaitCountEXT()) + " acquires=" +
                      std::to_string(acquires) + ")");
            JudgeNewValidation("A6 plain frames");

            SyncViewport(dev);
            PlainFrame(dev, frame_);
            const Frame f = ReadBackbuffer(dev);
            const Color c = (frame_ % 2 == 0) ? kBlue : kYellow;
            CheckStripes(f, { c, c, c, c }, "A7 the frame after the plain run renders exactly");
            JudgeNewValidation("A8 readback frame");
            ++frame_;
            return;
        }

        // ---- phase B: REMED-GFX-143 interleaving ---------------------------
        if (frame_ <= kPlainFrames + 1 + kInterleavedFrames)
        {
            InterleavedFrame(dev);
            if (frame_ == kPlainFrames + 1 + kInterleavedFrames)
            {
                const Frame f = ReadBackbuffer(dev);
                CheckStripes(f, { kRed, kRed, kGreen, kGreen },
                             "B1 backbuffer and render-target segments interleave correctly while "
                             "synchronization validation is enabled");
                JudgeNewValidation("B2 interleaved frames");
            }
            ++frame_;
            return;
        }

        // ---- phase BC: REMED-GFX-129 ordered clears -------------------------
        if (frame_ <= kPlainFrames + 1 + kInterleavedFrames + kClearFrames)
        {
            SyncViewport(dev);
            OrderedClearFrame(dev);
            if (frame_ == kPlainFrames + 1 + kInterleavedFrames + kClearFrames)
            {
                const Frame f = ReadBackbuffer(dev);
                CheckStripes(f, { kGreen, kGreen, kRed, kRed },
                             "BC1 an ordered backbuffer Clear() issued after geometry runs after "
                             "that geometry and before the geometry that follows it");
                JudgeNewValidation("BC2 ordered vkCmdClearAttachments frames");
            }
            ++frame_;
            return;
        }

        // ---- phase C: swapchain recreation ---------------------------------
        if (frame_ == kPlainFrames + 2 + kInterleavedFrames + kClearFrames)
        {
            recreatesBeforeResize_ = backend.GetSwapchainRecreateCountEXT();
            gdm_->setPreferredBackBufferWidthProperty(kBBW * 2);
            gdm_->setPreferredBackBufferHeightProperty(kBBH * 2);
            gdm_->ApplyChanges();
            SyncViewport(dev);
            PlainFrame(dev, frame_);
            ++frame_;
            return;
        }

        if (frame_ <= kPlainFrames + 2 + kInterleavedFrames + kClearFrames + kPostResizeFrames)
        {
            SyncViewport(dev);
            InterleavedFrame(dev);
            ++frame_;
            return;
        }

        // ---- final judgement ------------------------------------------------
        SyncViewport(dev);
        InterleavedFrame(dev);
        const Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kRed, kGreen, kGreen },
                     "C1 the same interleaving still renders exactly after the swapchain was "
                     "recreated at " + std::to_string(bbW_) + "x" + std::to_string(bbH_));
        check(backend.GetSwapchainRecreateCountEXT() > recreatesBeforeResize_,
              "C2 the resize really recreated the swapchain (" +
                  std::to_string(recreatesBeforeResize_) + " -> " +
                  std::to_string(backend.GetSwapchainRecreateCountEXT()) + ")");
        check(backend.GetDistinctAcquiredImageCountEXT() >= 2,
              "C3 the new swapchain's images are re-entered too (" +
                  std::to_string(backend.GetDistinctAcquiredImageCountEXT()) + " of " +
                  std::to_string(backend.GetSwapchainImageCountEXT()) + ")");
        JudgeNewValidation("C4 resize and post-resize frames");

        const Cardinality after = Cardinalities();
        check(after == baseline_,
              "D1 no synchronization object was created per frame or per recreation: " +
                  after.Text() + (after == baseline_ ? "" : " (baseline " + baseline_.Text() + ")"));

        const std::uint64_t acquires = backend.GetAcquireCountEXT();
        check(acquires > acquiresAtPlainEnd_ + static_cast<std::uint64_t>(kInterleavedFrames),
              "D2 the whole run rendered " + std::to_string(acquires) +
                  " acquired frames, well past a full wrap of the frame ring");
        check(backend.GetPresentCountEXT() == acquires &&
                  backend.GetFrameSubmitCountEXT() == acquires,
              "D3 one acquire, one frame submit and one present per rendered frame holds across "
              "interleaving, readback and recreation (acquire=" + std::to_string(acquires) +
                  " submit=" + std::to_string(backend.GetFrameSubmitCountEXT()) +
                  " present=" + std::to_string(backend.GetPresentCountEXT()) + ")");
        // Every SubmitFrame waits once; a deferred-readback submit waits a second time and an
        // out-of-date acquire waits without acquiring. Anything beyond that would be a new
        // per-frame CPU stall.
        check(backend.GetFrameFenceWaitCountEXT() <=
                  acquires + static_cast<std::uint64_t>(readbacks_) + 4,
              "D4 fence waits stay bounded by one per frame plus one per deferred readback -- no "
              "device-wide or per-segment wait was introduced (waits=" +
                  std::to_string(backend.GetFrameFenceWaitCountEXT()) + " acquires=" +
                  std::to_string(acquires) + " readbacks=" + std::to_string(readbacks_) + ")");

        std::printf("%d/%d checks passed on VULKAN\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    // BEFORE the Game is constructed: the backend creates its VkInstance during Game construction,
    // well before Initialize() runs, and VkValidationFeaturesEXT can only be supplied there. Asking
    // any later silently produces an instance WITHOUT synchronization validation, which would make
    // every hazard check in this file pass for the wrong reason -- measured, and the reason L1
    // asserts the layer separately from the hazard counts.
    VulkanGraphicsBackend::SetSyncValidationEnabledEXT(true);
    VulkanSwapchainSyncTest test;
    test.Run();
    return test.Result();
}
