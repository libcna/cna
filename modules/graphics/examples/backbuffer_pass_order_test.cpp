// SPDX-License-Identifier: MS-PL
// REMED-GFX-143: backbuffer work and render-target work must replay in ONE ordered stream.
//
// REMED-GFX-140 (Vulkan) and REMED-GFX-145 (SdlGpu) made every public render-target bind/unbind
// cycle its own native render pass, recorded in the order the cycles were opened. Both stopped at
// the same boundary: the BACKBUFFER stayed a single trailing pass at the end of the command buffer.
// `VulkanRenderer::RecordCommandBuffer` records every render-target segment first and then
// one swapchain pass which is passed `kAllSegments`; `SdlGpuRenderer::EnsureFrameRendered`
// walks `passSegments_` and then opens one swapchain pass which is passed `kSwapchainSegment`. In
// both, every backbuffer draw of the frame is replayed inside that one trailing pass regardless of
// when the game issued it, so the public sequence
//
//     SetRenderTarget(t); draw red;  SetRenderTarget(nullptr); draw t at LEFT
//     SetRenderTarget(t); draw green; SetRenderTarget(nullptr); draw t at RIGHT
//
// executes as
//
//     t pass (red) ; t pass (green) ; backbuffer pass (draw t at LEFT, draw t at RIGHT)
//
// and BOTH backbuffer draws sample the final green. The requested result is a red left and a green
// right. Nothing observable in `t` changes -- REMED-GFX-140's check `S6` covers exactly the
// interleaving sequence and passes -- which is why the class survived two segmentation fixes: the
// ordinary XNA pattern (render into targets, then compose them onto the backbuffer) is already the
// order a trailing pass produces. What breaks is a frame that consumes a target and then produces
// into it again.
//
// What this file measures, and why every check is falsifiable in both directions
// -----------------------------------------------------------------------------
// Deferred rendering may delay native submission for as long as it likes, but it must preserve the
// observable public order of draws, clears, render-target transitions and producer/consumer
// dependencies. The decisive shape needs three things at once:
//
//   1. a render target that is produced, consumed by the BACKBUFFER, then produced again;
//   2. the two backbuffer consumers landing in DIFFERENT places, so a reordered pair is not merely
//      a different colour but a different pixel layout;
//   3. NOTHING between the cycles -- no Present, no GetData, no flush, no fence, no sleep, no
//      extra frame. The only readback in each check happens after every command of that check has
//      been queued.
//
// Renderers whose backbuffer cannot be read publicly (SdlGpu has no `ReadBackbuffer` override at
// all, so `GraphicsDevice::GetBackBufferData` raises) declare `backbufferReadback ==
// Support::Unsupported` and their checks report an explicit skip naming the reason. That is a real
// hole in the public oracle for those renderers, recorded rather than papered over: their fix is
// measured structurally instead (native pass order and load actions under an interposer).
//
// Two behaviours that are NOT this task's subject are declared per renderer and asserted either
// way, so a failure never gets credited to or blamed on pass ordering:
//
//   * `mixedQueuesKeepPublicOrder` -- whether a SpriteBatch draw and a 3D draw issued inside ONE
//     bind cycle keep their relative public order. Vulkan holds sprites and 3D draws in two
//     separate queues (`activeBatches_`, `pending3D_`) and every pass replays all of the first and
//     then all of the second, so within one segment a 3D draw always lands on top of a sprite
//     issued after it. That is a distinct root cause from segment boundaries (SdlGpu already has
//     one unified `drawOrder_`), so check O3 declares it. What segmentation DOES fix is the
//     cross-segment case, O1/O2, which this file asserts unconditionally.
//   * `backbufferDepthOnlyClear` -- whether `Clear(ClearOptions::DepthBuffer, ...)` alone reaches
//     the backbuffer. `VulkanRenderer::ClearDepth` is the one clear entry point that does
//     not record a clear request at all, so a depth-only clear is dropped there.
//
// The palette is 0/255-only, an exact fixed point of sRGB encoding, so every comparison is
// byte-exact on every renderer including sRGB ones. Every asserted region is a full-height VERTICAL
// stripe, so nothing here depends on whether a renderer's backbuffer readback or render-target
// sampling is Y-flipped.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 64;  ///< Requested backbuffer width.
    constexpr int kBBH = 64;  ///< Requested backbuffer height.
    constexpr int kRT  = 8;   ///< Render-target edge (2D and cube). Every target has this size.
    constexpr int kStripes = 4;  ///< Vertical stripes the backbuffer is divided into.

    /// What a public readback does on this renderer.
    enum class Support
    {
        Exact,        ///< Returns the rendered surface byte for byte.
        Unsupported,  ///< Raises; the caller's destination is left untouched.
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        Support backbufferReadback;  ///< GraphicsDevice::GetBackBufferData.
        bool    rt2dTargets;         ///< RenderTarget2D can be created AND bound.
        Support rtReadback;          ///< RenderTarget2D::GetData at level 0.
        bool    cubeTargets;         ///< RenderTargetCube can be created AND bound.
        /**
         * REMED-GFX-143's own subject: backbuffer bind cycles are ordered segments in the same
         * stream as render-target segments. False is a declared, still-open defect on that
         * renderer; check A1 then asserts the COLLAPSED result so the declaration stays falsifiable
         * in both directions, and every other check that can only distinguish the two shapes
         * reports a skip naming the reason instead of a failure belonging to another task.
         */
        bool    orderedBackbufferSegments;
        /**
         * A SpriteBatch draw and a 3D draw issued inside ONE bind cycle keep their relative public
         * order. False where the renderer holds them in separate queues and replays one family
         * after the other -- a distinct root cause from segment boundaries (check O3).
         */
        bool    mixedQueuesKeepPublicOrder;
        /**
         * A `Clear()` issued AFTER a draw in the SAME backbuffer cycle wipes that draw. False on a
         * renderer whose only backbuffer clear mechanism is the render-pass load action, which
         * necessarily runs before every draw of its pass.
         */
        bool    clearAfterDrawWinsOnBackbuffer;
        /// `Clear(ClearOptions::DepthBuffer, ...)` alone reaches the backbuffer's depth buffer.
        bool    backbufferDepthOnlyClear;
        /// The backbuffer has a real depth buffer, so the depth-ordering checks mean something.
        bool    backbufferDepth;
        /**
         * A SpriteBatch fill on the BACKBUFFER honours a custom sub-Viewport, i.e. sprite
         * coordinates are viewport-local. Declared separately from the render-target answer
         * REMED-GFX-140 measures: a renderer can honour one and not the other.
         */
        bool    backbufferSpriteViewportIsLocal;
        /// A SpriteBatch fill on the BACKBUFFER honours GraphicsDevice.ScissorRectangle.
        bool    backbufferSpriteScissorApplies;
        /// 3D draws (BasicEffect over VertexPositionColor) rasterize here.
        bool    draws3D;
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing and its readback is REMED-GFX-127/130's deterministic refusal.
    // Every sequence must still be legal and must not throw.
    constexpr Contract kContract{"HEADLESS", Support::Unsupported, true, Support::Unsupported,
                                 true, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", Support::Exact, true, Support::Exact,
                                 false, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    constexpr Contract kContract{"EASYGL", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // `mixedQueuesKeepPublicOrder` was false until REMED-GFX-157. bgfx submits both families
    // immediately into a view, and bgfx's DEFAULT view mode radix-sorts a view's draws by sort key
    // to minimise state changes -- which discards submission order, so within ONE bind cycle a 3D
    // draw issued before a sprite still landed on top of it. Every view id is now set to
    // ViewMode::Sequential at init, the within-view half of REMED-GFX-155's between-view ordering,
    // and this declaration turned over; it was measured red the moment the fix landed.
    constexpr Contract kContract{"BGFX", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, false, true, true, false};
#elif defined(CNA_RENDERER_VULKAN)
    // REMED-GFX-143's primary subject. `orderedBackbufferSegments` was false until this task:
    // Phase 2 of RecordCommandBuffer was one trailing swapchain pass passed `kAllSegments`.
    // `mixedQueuesKeepPublicOrder` was false until REMED-GFX-157: `activeBatches_` and
    // `pending3D_` are two queues and every pass replayed all sprites and then all 3D draws, so
    // within ONE segment a 3D draw issued before a sprite still landed on top of it. Each pass now
    // splits its order range at every family change and replays the runs in order, and this
    // declaration turned over; it was measured red the moment the fix landed. `clearAfterDrawWinsOnBackbuffer` / `backbufferDepthOnlyClear` were BOTH false while
    // REMED-GFX-129 was open -- the clear colour reached the swapchain only through the render-pass
    // load op, and `ClearDepth` was the one clear entry point that recorded no clear request at
    // all. REMED-GFX-129 made Clear an ordered vkCmdClearAttachments command, so checks C4 and C6
    // now assert the correct behaviour on this renderer; both were measured red the moment the fix
    // landed, which is what turned these two declarations over.
    constexpr Contract kContract{"VULKAN", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `clearAfterDrawWinsOnBackbuffer` was false while REMED-GFX-156 was open: WebGPU delivered a
    // backbuffer clear colour only through the render-pass load op, the same mechanism
    // REMED-GFX-140 records for its render targets, so a Clear() after a draw inside ONE cycle
    // could not wipe that draw. REMED-GFX-156 put Clear into the ordered stream REMED-GFX-159 built
    // and cuts the cycle into one native pass per observable Clear, so check C4 now asserts the
    // correct behaviour here; it was measured red the moment the fix landed, which is what turned
    // this declaration over.
    // `backbufferSpriteScissorApplies` was false while REMED-GFX-146 was open, recorded by
    // REMED-GFX-143 as "a backbuffer SpriteBatch fill is not clipped by ScissorRectangle at all".
    // That description was the SYMPTOM, not the cause: the backbuffer pass was never missing its
    // scissor call, it resolved the rectangle from live state when it recorded the pass, and check
    // V2 below restores the full rectangle before its single read -- so every already-queued batch
    // was replayed unclipped. Its RENDER-TARGET scissor looked correct for the same reason in
    // reverse (a target switch IS the flush on this renderer, so live state still matched).
    // REMED-GFX-146 captures the whole scissor state per draw and this declaration turned over;
    // it was measured red the moment the fix landed.
    // `mixedQueuesKeepPublicOrder` was false from REMED-GFX-157 until REMED-GFX-159. WebGPU grouped
    // a bind cycle's draws by family too, but the OTHER way round -- all 3D draws, then all sprites
    // -- so `3D -> SpriteBatch` (check O3) looked correct here while `SpriteBatch -> 3D` (check O4)
    // was the one that inverted. That direction is why this declaration was `true` for as long as
    // O3 was the only mixed-family check in this file. REMED-GFX-159 replaced the fixed list of
    // per-family replay loops with one ordered reference stream, and this declaration turned over;
    // both checks were measured red the moment the fix landed.
    constexpr Contract kContract{"WEBGPU", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // SdlGpu has no `ReadBackbuffer` override, so `GetBackBufferData` raises and this file's
    // backbuffer oracle cannot run here at all. REMED-GFX-143's SdlGpu half is measured
    // structurally instead (native pass order and load actions under an interposer) and the
    // render-target-side checks below still run.
    constexpr Contract kContract{"SDL_GPU", Support::Unsupported, true, Support::Exact,
                                 true, true, true, false, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr Contract kContract{"SDL_RENDERER", Support::Exact, true, Support::Exact,
                                 false, true, true, true, true, false, false, true, false, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", Support::Exact, true, Support::Exact,
                                 false, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", Support::Exact, true, Support::Exact,
                                 false, true, true, true, true, false, false, false, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr Contract kContract{"DIRECTX9", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, true, true, false};
#else
#error "REMED-GFX-143: this renderer has no declared backbuffer command-order contract."
#endif

    /**
     * @brief When `mixedQueuesKeepPublicOrder` is false, which family this renderer replays FIRST.
     *
     * REMED-GFX-157: "does it keep public order" and "if not, which way does it group" are two
     * different facts, and O3 alone could only ever see the first. A renderer that replays all
     * sprites and then all 3D draws inverts `3D; sprite`; one that replays all 3D draws and then
     * all sprites inverts `sprite; 3D` instead. Only meaningful when the contract above says the
     * order is not public; checks O3 and O4 derive both of their predictions from the pair, so
     * there is exactly one place to change when a renderer is corrected.
     *
     * REMED-GFX-159 turned WEBGPU's `mixedQueuesKeepPublicOrder` over, and it was the last renderer
     * declaring anything but public order -- so nothing consults this today. It is kept, rather
     * than deleted along with the branch it feeds, because a renderer that regresses to a grouped
     * replay needs somewhere to say WHICH grouping, and rebuilding that distinction after the fact
     * is what cost REMED-GFX-157 its first measurement.
     */
    constexpr bool kMixedGroupingReplays3DFirst = false;

    /**
     * @brief Whether the SPRITE is the last of the pair to reach the target.
     *
     * @param spriteIssuedSecond True when the game issued the sprite after the 3D draw.
     * @return True when the sprite wins the overlapped stripe.
     */
    constexpr bool SpriteWinsMixedPair(bool spriteIssuedSecond)
    {
        return kContract.mixedQueuesKeepPublicOrder ? spriteIssuedSecond
                                                    : kMixedGroupingReplays3DFirst;
    }

    /// Fully saturated, so sRGB encoding is the identity on every asserted texel.
    const Color kBlack (  0,   0,   0, 255);
    const Color kRed   (255,   0,   0, 255);
    const Color kGreen (  0, 255,   0, 255);
    const Color kBlue  (  0,   0, 255, 255);
    const Color kYellow(255, 255,   0, 255);
    const Color kMagenta(255, 0,  255, 255);
    const Color kCyan  (  0, 255, 255, 255);

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// One classified backbuffer readback: what the call did, without judging it yet.
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
}

class BackbufferPassOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;
    /// One 1x1 opaque white texel; SpriteBatch tints it, so one texture serves every colour.
    std::unique_ptr<Texture2D> white_;
    /// Per-stripe clip-space quads at two depths, so a 3D draw can own exactly one stripe.
    std::array<std::unique_ptr<VertexBuffer>, kStripes> nearStripeVb_{};
    std::array<std::unique_ptr<VertexBuffer>, kStripes> farStripeVb_{};
    /// Full-width clip-space quads at two depths, for the backbuffer depth-ordering checks.
    std::unique_ptr<VertexBuffer> nearFullVb_;
    std::unique_ptr<VertexBuffer> farFullVb_;

    int bbW_ = kBBW;
    int bbH_ = kBBH;
    int phase_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void skip(const std::string& label) { check(true, label); }

    /**
     * @brief Whether this renderer's own pixels can judge a backbuffer question.
     *
     * A renderer with no public backbuffer readback cannot answer any question in this file about
     * backbuffer content. Every check still ISSUES its whole public command sequence there -- that
     * is what a structural oracle (native pass order and load actions under an interposer) needs to
     * observe, and issuing it also proves the sequence is legal and does not throw. Only the pixel
     * judgement is skipped, and the skip names the gap so it is visible in the output rather than
     * silently counted as a pass for a reason nobody wrote down.
     */
    bool CanJudgeBackbuffer(const std::string& label)
    {
        if (kContract.backbufferReadback == Support::Exact) return true;
        skip(label + ": sequence issued, judgement skipped -- this renderer has no public backbuffer "
                     "readback, so backbuffer content cannot be observed at all");
        return false;
    }

    /**
     * @brief Whether a check that can only distinguish an ordered stream from a trailing pass counts.
     *
     * Check A1 still runs on a renderer that declares the defect and asserts the COLLAPSED shape, so
     * the declaration turns red the day the renderer is fixed; every other such check reports a skip
     * naming the reason rather than a failure that belongs to a different task.
     */
    bool CanJudgeOrder(const std::string& label)
    {
        if (kContract.orderedBackbufferSegments) return true;
        skip(label + ": sequence issued, judgement skipped -- this renderer replays all backbuffer "
                     "work in one trailing pass (declared open defect)");
        return false;
    }

    // ---------------------------------------------------------------------
    // Geometry
    // ---------------------------------------------------------------------

    /// Six clip-space vertices covering x in [@p x0, @p x1] over the full height, at depth @p z.
    static std::array<VertexPositionColor, 6> Quad(float x0, float x1, float z, const Color& c)
    {
        return {{
            {Vector3(x0,  1.0f, z), c},
            {Vector3(x0, -1.0f, z), c},
            {Vector3(x1, -1.0f, z), c},
            {Vector3(x0,  1.0f, z), c},
            {Vector3(x1, -1.0f, z), c},
            {Vector3(x1,  1.0f, z), c},
        }};
    }

    std::unique_ptr<VertexBuffer> MakeVb(GraphicsDevice& dev,
                                         const std::array<VertexPositionColor, 6>& v)
    {
        auto vb = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), static_cast<int>(v.size()),
            BufferUsage::None);
        vb->SetData(v.data(), 0, static_cast<int>(v.size()));
        return vb;
    }

    // ---------------------------------------------------------------------
    // Backbuffer geometry in stripe units
    // ---------------------------------------------------------------------

    [[nodiscard]] int StripeX(int stripe) const { return stripe * bbW_ / kStripes; }
    [[nodiscard]] int StripeW() const { return bbW_ / kStripes; }

    /// The full-height destination rectangle of stripes [@p from, @p to).
    [[nodiscard]] Rectangle StripeRect(int from, int to) const
    {
        return Rectangle(StripeX(from), 0, StripeX(to) - StripeX(from), bbH_);
    }

    // ---------------------------------------------------------------------
    // Draw helpers -- each issues work into whatever is currently bound
    // ---------------------------------------------------------------------

    void BeginSprites()
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
    }

    void BeginSpritesScissored(RasterizerState& scissored)
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &scissored);
    }

    /// One SpriteBatch cycle filling backbuffer stripes [@p from, @p to) with @p colour.
    void SpriteStripes(int from, int to, const Color& colour)
    {
        BeginSprites();
        sb_->Draw(*white_, StripeRect(from, to), Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    /// One SpriteBatch cycle sampling @p src into backbuffer stripes [@p from, @p to).
    void SpriteTargetStripes(Texture2D& src, int from, int to)
    {
        BeginSprites();
        sb_->Draw(src, StripeRect(from, to), Rectangle(0, 0, kRT, kRT), Color::White);
        sb_->End();
    }

    /// Fills the whole currently-bound 8x8 render target with @p colour.
    void FillTarget(const Color& colour)
    {
        BeginSprites();
        sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), colour);
        sb_->End();
    }

    void Prepare3D(GraphicsDevice& dev, bool depth)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        // DepthStencilState, not the CNAEXT SetDepthTestEnabled/SetDepthWriteEnabled pair: bgfx
        // raises outright from those ("not yet wired into bgfx state flags"), and this is the XNA
        // route anyway. Default = test and write enabled, None = neither.
        DepthStencilState ds = depth ? DepthStencilState::Default : DepthStencilState::None;
        dev.setDepthStencilStateProperty(ds);
        fx_->Apply();
    }

    /// One non-indexed 3D draw of a whole 6-vertex quad buffer.
    void Draw3D(GraphicsDevice& dev, VertexBuffer& vb, bool depth)
    {
        Prepare3D(dev, depth);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    /// A complete produce cycle: bind @p rt, fill it with @p colour, unbind.
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Color& colour)
    {
        dev.SetRenderTarget(&rt);
        FillTarget(colour);
        dev.SetRenderTarget(nullptr);
    }

    /**
     * @brief One 8x8 target with @p usage.
     *
     * `DepthFormat::None` deliberately: a target that owns no depth buffer at all cannot be the
     * source of a depth value, so check D2 -- "a depth write inside a render-target pass does not
     * reject a later backbuffer draw" -- can only fail if the renderer attached the BACKBUFFER's
     * depth attachment to the target's pass, which is exactly the leak worth catching.
     */
    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, RenderTargetUsage usage)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, usage);
    }

    // ---------------------------------------------------------------------
    // Observation -- the ONLY readback in a check, after everything is queued
    // ---------------------------------------------------------------------

    Frame ReadBackbuffer(GraphicsDevice& dev)
    {
        Frame f;
        f.w = bbW_;
        f.h = bbH_;
        f.px.assign(static_cast<std::size_t>(bbW_) * bbH_, Color(0xCD, 0xCD, 0xCD, 0xCD));
        Rectangle all(0, 0, bbW_, bbH_);
        try
        {
            dev.GetBackBufferData(&all, f.px.data(), 0, static_cast<int>(f.px.size()));
        }
        catch (const std::exception& e)
        {
            f.threw = true;
            f.what = e.what();
        }
        catch (...)
        {
            f.threw = true;
            f.what = "unknown exception";
        }
        return f;
    }

    /**
     * @brief Asserts the four stripes of @p f are exactly @p expected, byte for byte.
     *
     * Each stripe is probed at three heights and two horizontal positions well inside it, so a
     * one-pixel rasterization or rounding difference at a stripe seam cannot decide a check while
     * a reordered draw still cannot hide.
     */
    void CheckStripes(const Frame& f, const std::array<Color, kStripes>& expected,
                      const std::string& label, bool needsOrder = true)
    {
        if (!CanJudgeBackbuffer(label)) return;
        if (needsOrder && !CanJudgeOrder(label)) return;
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

    /// Asserts a render target reads back as one uniform @p colour.
    void CheckTargetUniform(RenderTarget2D& rt, const Color& colour, const std::string& label)
    {
        if (kContract.rtReadback != Support::Exact)
        {
            skip(label + ": skipped -- no render-target readback on this renderer");
            return;
        }
        std::vector<Color> px(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
        rt.GetData(0, nullptr, px.data(), 0, static_cast<int>(px.size()));
        std::size_t bad = 0;
        std::string firstBad;
        for (std::size_t i = 0; i < px.size(); ++i)
        {
            if (Same(px[i], colour)) continue;
            ++bad;
            if (firstBad.empty())
                firstBad = " first at " + std::to_string(i) + " expected " + ColorText(colour) +
                           " got " + ColorText(px[i]);
        }
        check(bad == 0, bad == 0 ? label
                                 : label + ": " + std::to_string(bad) + " texels wrong," + firstBad);
    }

    // =====================================================================
    // A -- producer / consumer across backbuffer cycles (the decisive shape)
    // =====================================================================

    /**
     * @brief A1 -- target produced, consumed by the backbuffer, produced again, consumed again.
     *
     * The one check that runs on every renderer with a readable backbuffer regardless of the
     * declaration, so a renderer claiming the defect stays falsifiable: it asserts the COLLAPSED
     * result there and the correct one everywhere else.
     */
    void RunProduceConsumeProduce(GraphicsDevice& dev)
    {
        const std::string label = "A1 produce/consume/produce";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *t, kRed);
        SpriteTargetStripes(*t, 0, 2);
        ProduceInto(dev, *t, kGreen);
        SpriteTargetStripes(*t, 2, 4);
        Frame f = ReadBackbuffer(dev);

        if (kContract.orderedBackbufferSegments)
            CheckStripes(f, { kRed, kRed, kGreen, kGreen },
                         label + ": each backbuffer cycle sees the target as it stood when that "
                                 "cycle was issued", false);
        else
            CheckStripes(f, { kGreen, kGreen, kGreen, kGreen },
                         label + ": this renderer replays every backbuffer draw AFTER every target "
                                 "pass, so both consumers see the FINAL target content (declared "
                                 "open defect)", false);
    }

    /**
     * @brief A2 -- the forward order, which a trailing backbuffer pass already produces.
     *
     * A guard against a fix that reorders in the other direction: the ordinary XNA pattern must
     * keep working exactly.
     */
    void RunForwardConsume(GraphicsDevice& dev)
    {
        const std::string label = "A2 forward consume";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        SpriteStripes(0, 1, kRed);
        ProduceInto(dev, *t, kGreen);
        SpriteTargetStripes(*t, 1, 2);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlack, kBlack },
                     label + ": a backbuffer draw issued before a producer stays before it, and "
                             "the later consumer still sees the produced content", false);
    }

    /**
     * @brief A3 -- overlapping consumers, so a reordered pair changes the layout, not just a hue.
     */
    void RunOverlappingConsumers(GraphicsDevice& dev)
    {
        const std::string label = "A3 overlapping consumers";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *t, kRed);
        SpriteTargetStripes(*t, 0, 3);
        ProduceInto(dev, *t, kGreen);
        SpriteTargetStripes(*t, 2, 4);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kRed, kGreen, kGreen },
                     label + ": the second cycle's draw covers the first cycle's in the overlap "
                             "and nowhere else");
    }

    /// A4 -- three produce/consume cycles: backbuffer A -> target -> B -> target -> C.
    void RunThreeCycles(GraphicsDevice& dev)
    {
        const std::string label = "A4 three cycles";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *t, kRed);
        SpriteTargetStripes(*t, 0, 1);
        ProduceInto(dev, *t, kGreen);
        SpriteTargetStripes(*t, 1, 2);
        ProduceInto(dev, *t, kBlue);
        SpriteTargetStripes(*t, 2, 3);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlue, kBlack },
                     label + ": three backbuffer cycles each see their own producer's content");
    }

    /**
     * @brief A5 -- two targets of IDENTICAL dimensions, so no identity shortcut can pass this.
     *
     * Both targets are 8x8 with the same format and usage; only the resource differs. A renderer
     * that keyed a segment on size, format or "is a render target" rather than on the bind cycle
     * would collapse them.
     */
    void RunTwoIdenticalTargets(GraphicsDevice& dev)
    {
        const std::string label = "A5 two identical targets";
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *a, kRed);
        ProduceInto(dev, *b, kGreen);
        SpriteTargetStripes(*a, 0, 1);
        SpriteTargetStripes(*b, 1, 2);
        ProduceInto(dev, *a, kBlue);
        SpriteTargetStripes(*a, 2, 3);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlue, kBlack },
                     label + ": backbuffer -> target A -> target B -> backbuffer keeps every "
                             "consumer with its own producer");
        CheckTargetUniform(*a, kBlue, label + ": target A ends at its own final cycle");
        CheckTargetUniform(*b, kGreen, label + ": target B keeps its own content");
    }

    /**
     * @brief A6 -- an intervening backbuffer cycle must not disturb the targets themselves.
     *
     * REMED-GFX-140's `S6` measures this from the target side and passes even with a trailing
     * backbuffer pass; it is repeated here so this task's fix is measured against it rather than
     * credited with it, and so a regression in the other direction is caught.
     */
    void RunTargetsUnaffected(GraphicsDevice& dev)
    {
        const std::string label = "A6 targets unaffected";
        auto p = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto d = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *p, kRed);
        SpriteStripes(0, 1, kYellow);
        dev.SetRenderTarget(d.get());
        BeginSprites();
        sb_->Draw(*white_, Rectangle(0, 0, kRT / 2, kRT), Rectangle(0, 0, 1, 1), kMagenta);
        sb_->End();
        dev.SetRenderTarget(nullptr);
        SpriteStripes(1, 2, kCyan);
        Frame f = ReadBackbuffer(dev);
        (void) f;

        CheckTargetUniform(*p, kRed,
                           label + ": a PreserveContents target keeps its content across two "
                                   "interleaved backbuffer cycles");
        if (kContract.rtReadback == Support::Exact)
        {
            std::vector<Color> px(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
            d->GetData(0, nullptr, px.data(), 0, static_cast<int>(px.size()));
            bool ok = true;
            for (int y = 0; y < kRT; ++y)
                for (int x = 0; x < kRT; ++x)
                {
                    const Color& want = (x < kRT / 2) ? kMagenta : kBlack;
                    if (!Same(px[static_cast<std::size_t>(y) * kRT + x], want)) ok = false;
                }
            check(ok, ok ? label + ": a DiscardContents target still gets its own bind clear "
                           "between two backbuffer cycles"
                         : label + ": a DiscardContents target's bind clear was lost or its draw "
                                   "was misplaced between two backbuffer cycles");
        }
        else
        {
            skip(label + ": DiscardContents half skipped -- no render-target readback");
        }
    }

    // =====================================================================
    // C -- Clear, load and store per backbuffer cycle
    // =====================================================================

    /// C1 -- a Clear in a LATER backbuffer cycle wipes what an earlier cycle drew.
    void RunClearInLaterCycle(GraphicsDevice& dev)
    {
        const std::string label = "C1 clear in later cycle";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        SpriteStripes(0, 4, kBlue);
        ProduceInto(dev, *t, kMagenta);
        dev.Clear(kRed);
        SpriteStripes(0, 1, kGreen);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kGreen, kRed, kRed, kRed },
                     label + ": the second cycle's Clear wipes the first cycle's full-cover draw, "
                             "and only that cycle's own draw survives it");
    }

    /// C2 -- a Clear in the FIRST cycle must not be re-applied to a later one.
    void RunClearInFirstCycleOnly(GraphicsDevice& dev)
    {
        const std::string label = "C2 clear in first cycle only";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kRed);
        SpriteStripes(0, 1, kBlue);
        ProduceInto(dev, *t, kMagenta);
        SpriteStripes(1, 2, kGreen);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kBlue, kGreen, kRed, kRed },
                     label + ": a later backbuffer cycle LOADS what the earlier one stored instead "
                             "of clearing again", false);
    }

    /// C3 -- three cycles, a Clear in the middle one only.
    void RunClearInMiddleCycle(GraphicsDevice& dev)
    {
        const std::string label = "C3 clear in middle cycle";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        SpriteStripes(0, 1, kBlue);
        ProduceInto(dev, *t, kMagenta);
        dev.Clear(kGreen);
        ProduceInto(dev, *t, kCyan);
        SpriteStripes(3, 4, kYellow);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kGreen, kGreen, kGreen, kYellow },
                     label + ": the middle cycle's Clear wipes the first cycle's stripe and the "
                             "third cycle draws on top of the cleared surface");
    }

    /// C4 -- a Clear issued AFTER a draw inside ONE backbuffer cycle.
    void RunClearAfterDraw(GraphicsDevice& dev)
    {
        const std::string label = "C4 clear after draw in one cycle";

        dev.Clear(kBlack);
        SpriteStripes(0, 4, kBlue);
        dev.Clear(kRed);
        SpriteStripes(0, 1, kGreen);
        Frame f = ReadBackbuffer(dev);
        if (kContract.clearAfterDrawWinsOnBackbuffer)
            CheckStripes(f, { kGreen, kRed, kRed, kRed },
                         label + ": a Clear issued after a draw wipes that draw", false);
        else
            CheckStripes(f, { kGreen, kBlue, kBlue, kBlue },
                         label + ": this renderer's only backbuffer clear mechanism is the pass "
                                 "load action, so a Clear cannot wipe a draw issued before it "
                                 "inside the SAME cycle (declared, separate from segmentation)", false);
    }

    /// C5 -- a colour-only Clear in a later cycle must leave the depth buffer alone.
    void RunColourOnlyClearKeepsDepth(GraphicsDevice& dev)
    {
        const std::string label = "C5 colour-only clear keeps depth";
        if (!kContract.backbufferDepth || !kContract.draws3D)
        {
            skip(label + ": skipped -- no backbuffer depth buffer or no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        Draw3D(dev, *nearFullVb_, true);              // writes depth 0.3 everywhere
        ProduceInto(dev, *t, kMagenta);
        dev.Clear(ClearOptions::Target, kBlue, 1.0f, 0);
        Draw3D(dev, *farFullVb_, true);               // depth 0.7 -- must still be rejected
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kBlue, kBlue, kBlue, kBlue },
                     label + ": the colour Clear applies, and the farther quad is still rejected "
                             "by the depth the earlier cycle wrote");
    }

    /// C6 -- `Clear(ClearOptions::DepthBuffer, ...)` alone in a later cycle.
    void RunDepthOnlyClearInLaterCycle(GraphicsDevice& dev)
    {
        const std::string label = "C6 depth-only clear in later cycle";
        if (!kContract.backbufferDepth || !kContract.draws3D)
        {
            skip(label + ": skipped -- no backbuffer depth buffer or no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        SpriteStripes(0, 1, kRed);
        Draw3D(dev, *nearStripeVb_[1], true);         // stripe 1 at depth 0.3
        ProduceInto(dev, *t, kMagenta);
        dev.Clear(ClearOptions::DepthBuffer, kYellow, 1.0f, 0);
        Draw3D(dev, *farStripeVb_[1], true);          // stripe 1 at depth 0.7
        Frame f = ReadBackbuffer(dev);
        if (kContract.backbufferDepthOnlyClear)
            CheckStripes(f, { kRed, kCyan, kBlack, kBlack },
                         label + ": the depth-only Clear resets depth without touching colour, so "
                                 "the farther quad is accepted and the first cycle's stripe stays");
        else
            CheckStripes(f, { kRed, kGreen, kBlack, kBlack },
                         label + ": this renderer drops a depth-only Clear entirely, so the farther "
                                 "quad stays rejected -- what is asserted is that COLOUR was not "
                                 "wiped by it (declared, separate from segmentation)");
    }

    // =====================================================================
    // D -- backbuffer depth across cycles
    // =====================================================================

    /// D1 -- depth written in one backbuffer cycle still rejects a farther draw in a later one.
    void RunDepthSurvivesInterveningTarget(GraphicsDevice& dev)
    {
        const std::string label = "D1 depth survives an intervening target";
        if (!kContract.backbufferDepth || !kContract.draws3D)
        {
            skip(label + ": skipped -- no backbuffer depth buffer or no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        Draw3D(dev, *nearStripeVb_[0], true);
        Draw3D(dev, *nearStripeVb_[1], true);
        ProduceInto(dev, *t, kMagenta);
        Draw3D(dev, *farStripeVb_[0], true);          // farther, same stripe -- rejected
        Draw3D(dev, *farStripeVb_[2], true);          // farther, EMPTY stripe -- accepted
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kGreen, kGreen, kCyan, kBlack },
                     label + ": the depth an earlier backbuffer cycle wrote is neither discarded "
                             "nor re-cleared by a later one, and untouched depth still accepts", false);
    }

    /// D2 -- the render target's own depth must not leak into the backbuffer's.
    void RunTargetDepthDoesNotLeak(GraphicsDevice& dev)
    {
        const std::string label = "D2 target depth does not leak";
        if (!kContract.backbufferDepth || !kContract.draws3D)
        {
            skip(label + ": skipped -- no backbuffer depth buffer or no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        // A NEAR draw inside the target's own pass, with depth on, then a FAR draw on the
        // backbuffer covering the same stripes: the target's depth is a different attachment and
        // must not reject it.
        dev.SetRenderTarget(t.get());
        Draw3D(dev, *nearFullVb_, true);
        dev.SetRenderTarget(nullptr);
        Draw3D(dev, *farStripeVb_[0], true);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kCyan, kBlack, kBlack, kBlack },
                     label + ": a depth write inside a render-target pass does not reject a later "
                             "backbuffer draw", false);
    }

    // =====================================================================
    // O -- SpriteBatch and 3D relative order
    // =====================================================================

    /// O1 -- backbuffer 3D, then a target, then a backbuffer SpriteBatch over the same stripes.
    void Run3DThenSpritesAcrossCycles(GraphicsDevice& dev)
    {
        const std::string label = "O1 3D then sprites across cycles";
        if (!kContract.draws3D)
        {
            skip(label + ": skipped -- no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        Draw3D(dev, *nearStripeVb_[0], false);        // green stripe 0
        Draw3D(dev, *nearStripeVb_[1], false);        // green stripe 1
        ProduceInto(dev, *t, kMagenta);
        SpriteStripes(1, 3, kBlue);                   // must land ON TOP of the 3D stripe 1
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kGreen, kBlue, kBlue, kBlack },
                     label + ": a SpriteBatch draw issued in a LATER backbuffer cycle covers a 3D "
                             "draw issued in an earlier one");
    }

    /// O2 -- backbuffer SpriteBatch, then a target, then a backbuffer 3D draw.
    void RunSpritesThen3DAcrossCycles(GraphicsDevice& dev)
    {
        const std::string label = "O2 sprites then 3D across cycles";
        if (!kContract.draws3D)
        {
            skip(label + ": skipped -- no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        SpriteStripes(0, 2, kBlue);
        ProduceInto(dev, *t, kMagenta);
        Draw3D(dev, *nearStripeVb_[1], false);        // must land ON TOP of the sprite stripe 1
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kBlue, kGreen, kBlack, kBlack },
                     label + ": a 3D draw issued in a LATER backbuffer cycle covers a SpriteBatch "
                             "draw issued in an earlier one", false);
    }

    /// O3 -- the same pair INSIDE one backbuffer cycle. Declared per renderer; not this task.
    void RunMixedQueuesInOneCycle(GraphicsDevice& dev)
    {
        const std::string label = "O3 mixed queues in one cycle";
        if (!kContract.draws3D)
        {
            skip(label + ": skipped -- no 3D rasterization here");
            return;
        }

        dev.Clear(kBlack);
        Draw3D(dev, *nearStripeVb_[0], false);        // green stripe 0
        SpriteStripes(0, 1, kBlue);                   // issued after, same stripe
        Frame f = ReadBackbuffer(dev);
        // The sprite was issued SECOND, so public order puts it on top.
        CheckStripes(f, { SpriteWinsMixedPair(true) ? kBlue : kGreen, kBlack, kBlack, kBlack },
                     label + (kContract.mixedQueuesKeepPublicOrder
                                  ? ": a sprite issued after a 3D draw inside ONE cycle covers it"
                                  : ": this renderer groups a cycle's draws by family, so the winner "
                                    "is fixed by the grouping and not by public order (declared "
                                    "open defect with its own root cause, not segmentation)"),
                     false);
    }

    /**
     * @brief O4 -- the OTHER direction of the same pair, which O3 alone could never see.
     *
     * REMED-GFX-157: O3 only ever measured `3D draw; sprite`. A renderer that replays all sprites
     * and then all 3D draws produces the CORRECT picture for `sprite; 3D draw` by accident, so half
     * of the mixed-queue contract went unmeasured for as long as O3 was the only check -- and a
     * renderer that groups the other way round (all 3D, then all sprites) passed O3 outright while
     * inverting this direction. Both directions are now asserted, and the same per-renderer
     * declaration governs both, so neither grouping can hide behind the other.
     */
    void RunMixedQueuesInOneCycleReversed(GraphicsDevice& dev)
    {
        const std::string label = "O4 mixed queues in one cycle, sprite first";
        if (!kContract.draws3D)
        {
            skip(label + ": skipped -- no 3D rasterization here");
            return;
        }

        dev.Clear(kBlack);
        SpriteStripes(0, 1, kBlue);                   // blue stripe 0
        Draw3D(dev, *nearStripeVb_[0], false);        // issued after, same stripe: must cover it
        Frame f = ReadBackbuffer(dev);
        // The sprite was issued FIRST, so public order puts the 3D draw on top.
        CheckStripes(f, { SpriteWinsMixedPair(false) ? kBlue : kGreen, kBlack, kBlack, kBlack },
                     label + (kContract.mixedQueuesKeepPublicOrder
                                  ? ": a 3D draw issued after a sprite inside ONE cycle covers it"
                                  : ": this renderer groups a cycle's draws by family, so the winner "
                                    "is fixed by the grouping and not by public order (declared "
                                    "open defect)"),
                     false);
    }

    // =====================================================================
    // V -- viewport, scissor and state per backbuffer cycle
    // =====================================================================

    /// V1 -- three backbuffer cycles under three different sub-Viewports, A -> B -> A.
    void RunViewportPerCycle(GraphicsDevice& dev)
    {
        const std::string label = "V1 viewport per cycle";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        const int q = StripeW();

        dev.Clear(kBlack);
        dev.setViewportProperty(Viewport(StripeX(0), 0, q, bbH_));
        BeginSprites();
        sb_->Draw(*white_, Rectangle(0, 0, q, bbH_), Rectangle(0, 0, 1, 1), kRed);
        sb_->End();
        ProduceInto(dev, *t, kMagenta);               // resets Viewport to the full backbuffer
        dev.setViewportProperty(Viewport(StripeX(1), 0, q, bbH_));
        BeginSprites();
        sb_->Draw(*white_, Rectangle(0, 0, q, bbH_), Rectangle(0, 0, 1, 1), kGreen);
        sb_->End();
        ProduceInto(dev, *t, kCyan);
        dev.setViewportProperty(Viewport(StripeX(0), 0, q, bbH_));
        BeginSprites();
        sb_->Draw(*white_, Rectangle(0, 0, q / 2, bbH_), Rectangle(0, 0, 1, 1), kBlue);
        sb_->End();
        dev.setViewportProperty(Viewport(0, 0, bbW_, bbH_));
        Frame f = ReadBackbuffer(dev);

        if (!CanJudgeBackbuffer(label)) return;
        if (!CanJudgeOrder(label)) return;
        if (kContract.backbufferSpriteViewportIsLocal)
        {
            // Stripe 0's left half is the third cycle's blue, its right half the first cycle's
            // red; probe positions inside stripe 0 straddle that split, so assert it directly.
            const bool ok = !f.threw &&
                Same(f.At(q / 8, bbH_ / 2), kBlue) &&
                Same(f.At(q - q / 8, bbH_ / 2), kRed) &&
                Same(f.At(StripeX(1) + q / 2, bbH_ / 2), kGreen) &&
                Same(f.At(StripeX(2) + q / 2, bbH_ / 2), kBlack);
            check(ok, ok ? label + ": each cycle's own sub-Viewport applies to that cycle only"
                         : label + ": a cycle's sub-Viewport did not apply to that cycle only"
                                   + (f.threw ? (" (readback raised: " + f.what + ")")
                                              : std::string()));
        }
        else
        {
            skip(label + ": skipped -- this renderer's SpriteBatch ignores a sub-Viewport, so the "
                         "three cycles cannot be told apart by position");
        }
    }

    /// V2 -- three backbuffer cycles under three different ScissorRectangles, A -> B -> A.
    void RunScissorPerCycle(GraphicsDevice& dev)
    {
        const std::string label = "V2 scissor per cycle";
        if (!kContract.backbufferSpriteScissorApplies)
        {
            skip(label + ": skipped -- this renderer's SpriteBatch ignores ScissorRectangle");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);

        dev.Clear(kBlack);
        dev.setScissorRectangleProperty(StripeRect(0, 1));
        BeginSpritesScissored(scissored);
        sb_->Draw(*white_, StripeRect(0, 4), Rectangle(0, 0, 1, 1), kRed);
        sb_->End();
        ProduceInto(dev, *t, kMagenta);               // resets ScissorRectangle
        dev.setScissorRectangleProperty(StripeRect(1, 2));
        BeginSpritesScissored(scissored);
        sb_->Draw(*white_, StripeRect(0, 4), Rectangle(0, 0, 1, 1), kGreen);
        sb_->End();
        ProduceInto(dev, *t, kCyan);
        dev.setScissorRectangleProperty(StripeRect(2, 3));
        BeginSpritesScissored(scissored);
        sb_->Draw(*white_, StripeRect(0, 4), Rectangle(0, 0, 1, 1), kBlue);
        sb_->End();
        dev.setScissorRectangleProperty(Rectangle(0, 0, bbW_, bbH_));
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlue, kBlack },
                     label + ": each cycle's own scissor clips that cycle only");
    }

    // =====================================================================
    // U -- transient upload isolation across backbuffer segments
    // =====================================================================

    /**
     * @brief U1 -- distinctive GEOMETRY, not distinctive colour, across five interleaved cycles.
     *
     * Every deferred renderer copies sprite and 3D vertex data into one per-frame arena while
     * recording, long before the submit that makes the GPU read it. Splitting the backbuffer into
     * several passes over that one arena is exactly the shape that let REMED-GFX-140's Vulkan
     * arena defect exist: a cursor that restarted per pass made an earlier pass draw a later
     * pass's geometry. Colour alone cannot see it (the tint lives in the vertex data too, but a
     * whole-surface fill looks the same wherever its vertices came from), so each cycle here owns
     * a DIFFERENT stripe: a swapped pair lands in the wrong place.
     */
    void RunUploadIsolation(GraphicsDevice& dev)
    {
        const std::string label = "U1 upload isolation";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        SpriteStripes(0, 1, kRed);
        ProduceInto(dev, *t, kMagenta);
        SpriteStripes(1, 2, kGreen);
        ProduceInto(dev, *t, kCyan);
        SpriteStripes(2, 3, kBlue);
        ProduceInto(dev, *t, kYellow);
        SpriteStripes(3, 4, kYellow);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlue, kYellow },
                     label + ": four backbuffer cycles over one shared vertex arena each keep "
                             "their own geometry");
    }

    /// U2 -- the same question for the 3D arena, with a different vertex buffer per cycle.
    void RunUploadIsolation3D(GraphicsDevice& dev)
    {
        const std::string label = "U2 upload isolation (3D)";
        if (!kContract.draws3D)
        {
            skip(label + ": skipped -- no 3D rasterization here");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        Draw3D(dev, *nearStripeVb_[0], false);
        ProduceInto(dev, *t, kMagenta);
        Draw3D(dev, *farStripeVb_[1], false);
        ProduceInto(dev, *t, kCyan);
        Draw3D(dev, *nearStripeVb_[2], false);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kGreen, kCyan, kGreen, kBlack },
                     label + ": three backbuffer 3D cycles over one shared arena each keep their "
                             "own geometry and colour");
    }

    // =====================================================================
    // M -- MRT, MSAA and cube targets interleaved with backbuffer cycles
    // =====================================================================

    /// M1 -- an MRT bind cycle between two backbuffer cycles, with a consumer of each attachment.
    void RunMrtInterleaved(GraphicsDevice& dev)
    {
        const std::string label = "M1 MRT interleaved";
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        std::vector<RenderTargetBinding> set;
        set.emplace_back(static_cast<Texture*>(a.get()));
        set.emplace_back(static_cast<Texture*>(b.get()));
        bool mrtSupported = true;
        try
        {
            dev.SetRenderTargets(set);
        }
        catch (...)
        {
            mrtSupported = false;
        }
        if (!mrtSupported)
        {
            try { dev.SetRenderTargets({}); } catch (...) {}
            skip(label + ": skipped -- this renderer refuses two simultaneous render targets");
            return;
        }
        FillTarget(kRed);
        dev.SetRenderTargets({});
        SpriteTargetStripes(*a, 0, 1);
        dev.SetRenderTargets(set);
        FillTarget(kGreen);
        dev.SetRenderTargets({});
        SpriteTargetStripes(*a, 1, 2);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kRed, kGreen, kBlack, kBlack },
                     label + ": each backbuffer consumer sees the MRT attachment as its own cycle "
                             "left it");
    }

    /// M2 -- a cube-face bind cycle between two backbuffer cycles.
    void RunCubeInterleaved(GraphicsDevice& dev)
    {
        const std::string label = "M2 cube interleaved";
        if (!kContract.cubeTargets)
        {
            skip(label + ": skipped -- no RenderTargetCube on this renderer");
            return;
        }
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto cube = std::make_unique<RenderTargetCube>(dev, kRT, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *t, kRed);
        SpriteTargetStripes(*t, 0, 1);
        dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        FillTarget(kYellow);
        dev.SetRenderTargets({});
        ProduceInto(dev, *t, kGreen);
        SpriteTargetStripes(*t, 1, 2);
        Frame f = ReadBackbuffer(dev);
        // A RenderTargetCube is a TextureCube, so SpriteBatch cannot sample it; the cube's own
        // face is the observable, and the backbuffer stripes prove the interleaving around it.
        CheckStripes(f, { kRed, kGreen, kBlack, kBlack },
                     label + ": a cube-face bind cycle between two backbuffer cycles does not "
                             "merge them");
        if (kContract.rtReadback == Support::Exact)
        {
            std::vector<Color> px(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
            bool ok = true;
            try
            {
                cube->GetData(CubeMapFace::PositiveX, 0, nullptr, px.data(), 0,
                              static_cast<int>(px.size()));
                for (const Color& c : px)
                    if (!Same(c, kYellow)) ok = false;
            }
            catch (...)
            {
                ok = false;
            }
            check(ok, ok ? label + ": the cube face keeps its own content"
                         : label + ": the cube face lost its own content");
        }
        else
        {
            skip(label + ": cube readback half skipped -- no readback on this renderer");
        }
    }

    // =====================================================================
    // R -- repetition and stability
    // =====================================================================

    /**
     * @brief R1 -- seventeen interleaved cycles in ONE frame.
     *
     * Alternating produce/consume seventeen times exercises the segment bookkeeping well past any
     * fixed-size assumption, and the final layout still names every cycle individually.
     */
    void RunManyCycles(GraphicsDevice& dev)
    {
        const std::string label = "R1 many cycles";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        const std::array<Color, 4> wheel = { kRed, kGreen, kBlue, kYellow };

        dev.Clear(kBlack);
        for (int i = 0; i < 17; ++i)
        {
            ProduceInto(dev, *t, wheel[static_cast<std::size_t>(i % 4)]);
            SpriteTargetStripes(*t, i % kStripes, (i % kStripes) + 1);
        }
        Frame f = ReadBackbuffer(dev);
        // Cycle i paints stripe (i % 4) with wheel[i % 4]; the LAST write to each stripe wins, and
        // since both indices advance together every stripe ends at its own wheel colour.
        CheckStripes(f, { kRed, kGreen, kBlue, kYellow },
                     label + ": seventeen interleaved cycles in one frame each keep their own "
                             "producer and their own destination");
    }

    /// R2 -- the decisive sequence again in a SECOND frame, after a real Present.
    void RunSecondFrame(GraphicsDevice& dev)
    {
        const std::string label = "R2 second frame";
        auto t = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.Clear(kBlack);
        ProduceInto(dev, *t, kBlue);
        SpriteTargetStripes(*t, 0, 2);
        ProduceInto(dev, *t, kYellow);
        SpriteTargetStripes(*t, 2, 4);
        Frame f = ReadBackbuffer(dev);
        CheckStripes(f, { kBlue, kBlue, kYellow, kYellow },
                     label + ": the ordering holds in a later frame of the same run");
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));

        // A 2D-only renderer raises from VertexBuffer's constructor outright ("SDL_Renderer does not
        // support 3D: CreateVertexBuffer"), so the 3D resources are built only where the contract
        // says 3D rasterizes at all. Every check that touches them is gated on the same flag.
        if (kContract.draws3D)
        {
            fx_ = std::make_unique<BasicEffect>(dev);
            fx_->VertexColorEnabled = true;
            fx_->setWorldProperty(Matrix::getIdentityProperty());
            fx_->setViewProperty(Matrix::getIdentityProperty());
            fx_->setProjectionProperty(Matrix::getIdentityProperty());

            for (int s = 0; s < kStripes; ++s)
            {
                const float x0 = -1.0f + static_cast<float>(s) * (2.0f / kStripes);
                const float x1 = x0 + (2.0f / kStripes);
                nearStripeVb_[static_cast<std::size_t>(s)] =
                    MakeVb(dev, Quad(x0, x1, 0.3f, kGreen));
                farStripeVb_[static_cast<std::size_t>(s)] =
                    MakeVb(dev, Quad(x0, x1, 0.7f, kCyan));
            }
            nearFullVb_ = MakeVb(dev, Quad(-1.0f, 1.0f, 0.3f, kGreen));
            farFullVb_  = MakeVb(dev, Quad(-1.0f, 1.0f, 0.7f, kCyan));
        }
    }

    void Draw(const GameTime&) override
    {
        // Frame 0 is a warm-up: bgfx copies every createTexture payload at bgfx::frame(), so
        // without it the 1x1 source would still be unuploaded during the first real Draw.
        if (phase_ == 0)
        {
            phase_ = 1;
            SpriteStripes(0, kStripes, kBlack);
            return;
        }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        const Viewport vp = dev.getViewportProperty();
        if (vp.getWidthProperty() > 0 && vp.getHeightProperty() > 0)
        {
            bbW_ = vp.getWidthProperty();
            bbH_ = vp.getHeightProperty();
        }
        std::printf("REMED-GFX-143 backbuffer command order -- renderer %s, backbuffer %dx%d\n",
                    kContract.name, bbW_, bbH_);

        if (!kContract.rt2dTargets)
        {
            skip("no RenderTarget2D on this renderer -- nothing to interleave");
        }
        else
        {
            RunProduceConsumeProduce(dev);
            RunForwardConsume(dev);
            RunOverlappingConsumers(dev);
            RunThreeCycles(dev);
            RunTwoIdenticalTargets(dev);
            RunTargetsUnaffected(dev);
            RunClearInLaterCycle(dev);
            RunClearInFirstCycleOnly(dev);
            RunClearInMiddleCycle(dev);
            RunClearAfterDraw(dev);
            RunColourOnlyClearKeepsDepth(dev);
            RunDepthOnlyClearInLaterCycle(dev);
            RunDepthSurvivesInterveningTarget(dev);
            RunTargetDepthDoesNotLeak(dev);
            Run3DThenSpritesAcrossCycles(dev);
            RunSpritesThen3DAcrossCycles(dev);
            RunMixedQueuesInOneCycle(dev);
        RunMixedQueuesInOneCycleReversed(dev);
            RunViewportPerCycle(dev);
            RunScissorPerCycle(dev);
            RunUploadIsolation(dev);
            RunUploadIsolation3D(dev);
            RunMrtInterleaved(dev);
            RunCubeInterleaved(dev);
            RunManyCycles(dev);
            RunSecondFrame(dev);
        }

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    BackbufferPassOrderTest test;
    test.Run();
    return test.Result();
}
