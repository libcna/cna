// SPDX-License-Identifier: MS-PL
// REMED-GFX-129: GraphicsDevice::Clear() is an ORDERED command at its exact public call position.
//
// XNA/FNA scope RenderTargetUsage to what happens when a target is SET. Clear() is a separate
// contract: it overwrites the selected attachments at the point in the command stream where it was
// called. The two must not be conflated, so
//
//     SetRenderTarget(preserving);  Draw(A);  Clear(B);  Draw(C);  SetRenderTarget(nullptr);
//
// must leave B everywhere C did not touch, and nothing of A anywhere -- on a PreserveContents
// target just as much as on a DiscardContents one.
//
// `VulkanRenderer` delivered a clear ONLY through the render pass load op:
// `GetOrCreateRTRenderPass(depthFormat, discardContents)` chose
// `discardContents ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD` and there was no
// `vkCmdClearAttachments` path anywhere in the renderer. A pass has exactly one load action, so
//
//   * a preserving pass (LOAD_OP_LOAD) had no mechanism to deliver a clear colour at all;
//   * a clear necessarily ran BEFORE every draw of its pass, so "draw, then Clear" could not wipe
//     the draw;
//   * two Clear()s in one cycle collapsed to one;
//   * `ClearDepth` recorded no clear request whatsoever, so a depth-only Clear() did nothing;
//   * a depth or stencil clear could not be issued without also taking the colour load action the
//     target's usage had already chosen.
//
// REMED-GFX-140 (per-bind-cycle segmentation) and REMED-GFX-143 (backbuffer/off-screen interleaving)
// do not reach any of that, which is why this is its own finding: check `O1` below has ONE bind
// cycle, so there is nothing to segment.
//
// Rules every check in this file obeys
// ------------------------------------
//   * The whole public sequence of a check is queued, and only THEN is anything read. No GetData,
//     GetBackBufferData, Present, flush, fence wait, sleep or extra frame sits between two commands
//     of one sequence -- a renderer must not be able to pass by being forced to settle in between.
//   * The palette is 0/255-only, an exact fixed point of sRGB encoding, so every colour comparison
//     is byte-exact on every renderer including sRGB ones.
//   * Depth and stencil are never read back directly. They are proven by RENDERING: geometry that
//     the stored depth must reject or accept, and geometry the stored stencil must gate. Absence of
//     a validation message proves nothing and is never used as evidence.
//   * Colour work and 3D work never share one bind cycle, so a renderer that replays sprites and 3D
//     draws in separate groups within a cycle is not being measured on that.
//
// The clear rectangle
// -------------------
// REMED-GFX-018 established this project's cross-renderer contract on Bgfx: "each clear is a
// full-target ordered operation ... viewport and scissor do not restrict Clear". Checks `V1`/`V2`
// assert exactly that and are declared per renderer, so a renderer that genuinely scopes Clear to the
// viewport is recorded rather than silently standardised away.
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
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

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
    constexpr int kBBW = 32;   ///< Backbuffer width.
    constexpr int kBBH = 16;   ///< Backbuffer height.
    constexpr int kRT  = 8;    ///< Render-target edge (2D and cube).
    constexpr int kHalf = kRT / 2;
    constexpr int kMsaaRequest = 4;

    const Color kRed    (255,   0,   0, 255);
    const Color kGreen  (  0, 255,   0, 255);
    const Color kBlue   (  0,   0, 255, 255);
    const Color kYellow (255, 255,   0, 255);
    const Color kMagenta(255,   0, 255, 255);
    const Color kCyan   (  0, 255, 255, 255);
    const Color kWhite  (255, 255, 255, 255);
    const Color kBlack  (  0,   0,   0, 255);

    /// Destination pre-fill: equal to no colour this file ever draws or clears with.
    Color Sentinel() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }

    /// The colour GraphicsDevice::SetRenderTarget clears a DiscardContents target to on bind.
    Color DiscardColour() { return kBlack; }

    /**
     * @brief The complete, reviewed per-renderer claim this file enforces.
     *
     * Every flag is asserted in BOTH directions: a check whose flag is false asserts the declared
     * defective shape, so the declaration turns red the day the renderer is fixed as well as the day
     * it regresses.
     */
    struct Contract
    {
        const char* name;
        bool rt2dTargets;          ///< RenderTarget2D can be created AND bound.
        bool rtReadback;           ///< RenderTarget2D::GetData returns the surface byte for byte.
        bool backbufferReadback;   ///< GetBackBufferData returns the backbuffer byte for byte.
        bool cubeTargets;          ///< RenderTargetCube can be created AND bound.
        bool cubeReadback;         ///< RenderTargetCube::GetData at level 0 is exact.
        bool primitives3D;         ///< DrawUserPrimitives renders into a RenderTarget2D.
        bool depthBuffer3D;        ///< A depth-tested 3D draw is gated by the stored depth.
        bool stencilBuffer3D;      ///< A stencil-gated 3D draw is gated by the stored stencil.
        /**
         * REMED-GFX-129's subject. True when Clear() is an ordered command: a Clear() issued after
         * a draw wipes that draw, a Clear() on a PreserveContents target is honoured, two Clear()s
         * in one bind cycle both happen, and a depth-only or stencil-only Clear() reaches the
         * buffer it names. False is a declared defect whose whole cause is that the renderer's only
         * clear mechanism is the render-pass load action.
         */
        bool orderedClear;
        /**
         * A Clear() on a PreserveContents target is honoured even where `orderedClear` is false --
         * true on a renderer that chooses the load action per bind cycle and can therefore deliver a
         * leading clear, but still cannot deliver one after a draw.
         */
        bool clearOnPreserveTarget;
        /**
         * Clear covers the whole target regardless of the current Viewport (REMED-GFX-018's
         * cross-renderer contract: "each clear is a full-target ordered operation ... viewport and
         * scissor do not restrict Clear"). False records a renderer that scopes it to the viewport.
         */
        bool clearIgnoresViewport;
        /// The same question for ScissorRectangle, declared separately because they can differ.
        bool clearIgnoresScissor;
        bool preferMultiSampling;  ///< A target only multisamples if the backbuffer does.
        bool msaaTargetReadback;   ///< A multisampled RenderTarget2D reads back its RESOLVED content.
        bool wantHiDefProfile;
    };

#if defined(CNA_RENDERER_VULKAN)
    // REMED-GFX-129's subject. `orderedClear` and `clearOnPreserveTarget` declare the CORRECT
    // contract from the start: this file was written red, with every ordered-Clear check failing on
    // the pre-fix renderer, precisely so the fix is measured against it instead of being credited
    // with it.
    constexpr Contract kContract{"VULKAN", true, true, true, true, true,
                                 true, true, true,
                                 true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    // `clearIgnoresScissor` false is an INDEPENDENT, separately recorded divergence, not this
    // finding: EasyGL clears with glClear, which GL_SCISSOR_TEST narrows. Its Clear IS
    // viewport-independent, which is why the two questions are declared separately.
    constexpr Contract kContract{"EASYGL", true, true, true, true, true,
                                 true, true, true,
                                 true, true, true, false, false, true, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", true, true, true, false, false,
                                 true, true, false,
                                 true, true, true, true, false, true, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // SDL_gpu delivers a clear colour only through SDL_GPUColorTargetInfo.load_op, and a render pass
    // has exactly one. REMED-GFX-156 made the LOGICAL SEGMENT the unit that owns a load action
    // instead of the bind cycle: an observable Clear() closes the open segment and opens another one
    // over the same destination, so the clear is the new pass's load action and everything issued
    // after it joins that pass. `orderedClear` is therefore true and the whole declaration is
    // measured, not assumed.
    constexpr Contract kContract{"SDL_GPU", true, true, false, true, true,
                                 true, true, false,
                                 true, true, true, true, false, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // `msaaTargetReadback` was false while a multisampled RenderTarget2D reported a successful
    // readback over untouched memory; REMED-GFX-154 fixed that, so check S1 measures the ordered
    // Clear on a multisampled destination instead of skipping it.
    constexpr Contract kContract{"BGFX", true, true, true, true, true,
                                 true, true, true,
                                 true, true, true, true, false, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // wgpu delivers a clear only through the pass load op, exactly as SDL_gpu does, and a
    // WGPURenderPassDescriptor has exactly one set of them. REMED-GFX-156 put Clear() into the same
    // ordered stream REMED-GFX-159 built for the eleven draw families and cuts that stream into one
    // native pass per observable Clear, so `orderedClear` is true here too.
    constexpr Contract kContract{"WEBGPU", true, true, true, true, true,
                                 true, true, false,
                                 true, true, true, true, false, true, false};
#elif defined(CNA_RENDERER_HEADLESS)
    // Rasterizes nothing and owns no readable colour: every sequence must still be legal.
    constexpr Contract kContract{"HEADLESS", true, false, false, true, false,
                                 true, false, false,
                                 true, true, true, true, false, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    // `clearIgnoresViewport` / `clearIgnoresScissor` BOTH false, and both measured: D3D9's
    // IDirect3DDevice9::Clear is bounded by the current viewport and, with scissor testing enabled,
    // by the scissor rectangle -- which is real XNA's own behaviour, since XNA ran on D3D9. It
    // disagrees with REMED-GFX-018's cross-renderer contract and with the five other measurable
    // renderers; recorded as an independent finding rather than standardised away here.
    constexpr Contract kContract{"DIRECTX9", true, true, true, true, true,
                                 true, true, true,
                                 true, true, false, false, false, true, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, true, true, true, true,
                                 true, true, true,
                                 true, true, true, true, false, true, false};
#elif defined(CNA_RENDERER_LLGL)
    // Cube targets and stencil testing are deliberately unsupported on the validated OpenGL path;
    // their dedicated LLGL checks require deterministic NotSupportedException instead.
    // `preferMultiSampling` false: measured -- RenderTarget2D.MultiSampleCount applies regardless of
    // whether the device itself was constructed with PreferMultiSampling (LLGL reads a render
    // target's own requested sample count at ITS OWN construction, not the swap chain's).
    // `orderedClear`/`clearOnPreserveTarget` true: every command this renderer queues, including
    // Clear(), replays in original public order within its own target's bucket.
    constexpr Contract kContract{"LLGL", true, true, true, false, false,
                                 true, true, false,
                                 true, true, true, true, false, true, false};
#else
#error "REMED-GFX-129: this renderer has no declared ordered-Clear contract."
#endif

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

    /// One classified readback attempt: what the call did, without judging it yet.
    struct Probe
    {
        bool threw = false;
        std::string what;
        std::vector<Color> dest;
        std::size_t sentinelSurvivors = 0;
    };
}

class OrderedClearTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<Texture2D> white_;

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

    // ---------------------------------------------------------------------
    // Colour producers -- SpriteBatch only, so no depth or stencil is involved
    // ---------------------------------------------------------------------

    void BeginQuads()
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
    }

    /// Fills the rectangle (@p x, @p y, @p w, @p h) of whatever is bound, in one batch.
    void Fill(const Color& c, int x, int y, int w, int h)
    {
        BeginQuads();
        sb_->Draw(*white_, Rectangle(x, y, w, h), Rectangle(0, 0, 1, 1), c);
        sb_->End();
    }

    void FillTarget(const Color& c)      { Fill(c, 0, 0, kRT, kRT); }
    void FillLeftHalf(const Color& c)    { Fill(c, 0, 0, kHalf, kRT); }
    void FillBackbuffer(const Color& c)  { Fill(c, 0, 0, kBBW, kBBH); }
    void FillBackbufferLeft(const Color& c) { Fill(c, 0, 0, kBBW / 2, kBBH); }

    // ---------------------------------------------------------------------
    // 3D producers -- the depth and stencil oracles
    // ---------------------------------------------------------------------

    /// Six clip-space vertices covering x in [@p x0, @p x1], the whole height, at depth @p z.
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

    /// One 3D draw of the clip-space span [@p x0, @p x1] at depth @p z under @p state.
    void Draw3D(GraphicsDevice& dev, const DepthStencilState& state, float x0, float x1, float z,
                const Color& c)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        DepthStencilState copy = state;
        dev.setDepthStencilStateProperty(copy);
        fx_->Apply();
        const std::array<VertexPositionColor, 6> v = Quad(x0, x1, z, c);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v.data(), 0, 2);
    }

    /// Depth test on, depth write on, LessEqual -- the ordinary 3D state.
    static DepthStencilState DepthState()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        return ds;
    }

    /// Writes @p reference into the stencil buffer everywhere it draws. No depth involvement.
    static DepthStencilState StencilWriteState(int reference)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(reference);
        return ds;
    }

    /// Draws only where the stencil buffer already equals @p reference. No depth involvement.
    static DepthStencilState StencilTestState(int reference)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setReferenceStencilProperty(reference);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        return ds;
    }

    // ---------------------------------------------------------------------
    // Readback and judgement
    // ---------------------------------------------------------------------

    Probe ReadTarget(const RenderTarget2D& rt)
    {
        Probe p;
        p.dest.assign(static_cast<std::size_t>(kRT) * kRT, Sentinel());
        try { rt.GetData(0, nullptr, p.dest.data(), 0, static_cast<int>(p.dest.size())); }
        catch (const std::exception& e) { p.threw = true; p.what = e.what(); }
        catch (...) { p.threw = true; p.what = "unknown"; }
        for (const Color& c : p.dest) if (Same(c, Sentinel())) ++p.sentinelSurvivors;
        return p;
    }

    Probe ReadFace(const RenderTargetCube& cube, int face)
    {
        Probe p;
        p.dest.assign(static_cast<std::size_t>(kRT) * kRT, Sentinel());
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), 0, nullptr, p.dest.data(), 0,
                         static_cast<int>(p.dest.size()));
        }
        catch (const std::exception& e) { p.threw = true; p.what = e.what(); }
        catch (...) { p.threw = true; p.what = "unknown"; }
        for (const Color& c : p.dest) if (Same(c, Sentinel())) ++p.sentinelSurvivors;
        return p;
    }

    Probe ReadBackbuffer(GraphicsDevice& dev)
    {
        Probe p;
        p.dest.assign(static_cast<std::size_t>(kBBW) * kBBH, Sentinel());
        try { dev.GetBackBufferData(nullptr, p.dest.data(), 0, static_cast<int>(p.dest.size())); }
        catch (const std::exception& e) { p.threw = true; p.what = e.what(); }
        catch (...) { p.threw = true; p.what = "unknown"; }
        for (const Color& c : p.dest) if (Same(c, Sentinel())) ++p.sentinelSurvivors;
        return p;
    }

    /// Asserts every texel of a @p w x @p h probe equals @p expected(x, y), byte-exact.
    template <typename Fn>
    void Expect(const Probe& p, int w, int h, Fn expected, const std::string& label)
    {
        if (p.threw)
        {
            check(false, label + " [readback threw: " + p.what + "]");
            return;
        }
        std::size_t bad = 0;
        std::string first;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Color want = expected(x, y);
                const Color& got = p.dest[static_cast<std::size_t>(y) * w + x];
                if (!Same(got, want))
                {
                    ++bad;
                    if (first.empty())
                        first = " first@(" + std::to_string(x) + "," + std::to_string(y) +
                                ") got " + ColorText(got) + " want " + ColorText(want);
                }
            }
        check(bad == 0, label + " [mismatched=" + std::to_string(bad) + "/" +
                            std::to_string(static_cast<std::size_t>(w) * h) + first + "]");
    }

    void ExpectUniform(const Probe& p, int w, int h, const Color& c, const std::string& label)
    {
        Expect(p, w, h, [&c](int, int) { return c; }, label);
    }

    /// Asserts the left half is @p left and the right half is @p right, byte-exact.
    void ExpectHalves(const Probe& p, int w, int h, const Color& left, const Color& right,
                      const std::string& label)
    {
        Expect(p, w, h, [&](int x, int) { return x < w / 2 ? left : right; }, label);
    }

    // ---------------------------------------------------------------------
    // Construction helpers
    // ---------------------------------------------------------------------

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, RenderTargetUsage usage,
                                              DepthFormat depth = DepthFormat::None,
                                              int multiSampleCount = 0)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color, depth,
                                                multiSampleCount, usage);
    }

    bool TryBindMrt(GraphicsDevice& dev, RenderTarget2D& a, RenderTarget2D& b)
    {
        try
        {
            dev.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&a)),
                                  RenderTargetBinding(static_cast<Texture*>(&b))});
            return true;
        }
        catch (...)
        {
            try { dev.SetRenderTargets({}); } catch (...) {}
            return false;
        }
    }

    /// Gate for any check whose evidence is a render-target readback.
    bool NeedRtReadback(const std::string& label)
    {
        if (kContract.rt2dTargets && kContract.rtReadback) return true;
        skip(label + ": skipped -- this renderer has no exact RenderTarget2D readback");
        return false;
    }

    // =====================================================================
    // O -- colour Clear ordering on a RenderTarget2D
    // =====================================================================

    /**
     * @brief O1 -- draw, then Clear, in the ONLY bind cycle of a PreserveContents target.
     *
     * The whole finding in one shape. One cycle means there is nothing to segment, so a failure
     * here cannot be attributed to REMED-GFX-140 or REMED-GFX-143.
     */
    void RunDrawThenClearSingleCycle(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O1 draw then Clear")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectUniform(p, kRT, kRT, kGreen,
                          "O1 draw then Clear: a Clear() issued after a draw, in the only bind "
                          "cycle of a PreserveContents target, wipes that draw");
        else
            ExpectUniform(p, kRT, kRT, kRed,
                          "O1 draw then Clear: this renderer's only clear mechanism is the pass load "
                          "action, so the earlier draw survives (declared REMED-GFX-129 defect)");
    }

    /// O2 -- Clear, then a PARTIAL draw: the clear colour must survive outside the draw.
    void RunClearThenPartialDraw(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O2 Clear then partial draw")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.clearOnPreserveTarget)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "O2 Clear then partial draw: the clear colour survives everywhere the "
                         "later draw did not touch");
        else
            // The freshly created preserving target has never been written, so what shows outside
            // the marker is the renderer's own untouched surface -- asserted as "not the clear
            // colour", which is exactly the claim, and the marker must still have landed.
            check(!p.threw && Same(p.dest[0], kBlue) &&
                      !Same(p.dest[static_cast<std::size_t>(kRT) - 1], kGreen),
                  "O2 Clear then partial draw: the leading Clear() is DROPPED here (declared "
                  "defect) -- marker=" + ColorText(p.dest[0]) + " background=" +
                      ColorText(p.dest[static_cast<std::size_t>(kRT) - 1]));
    }

    /// O3 -- draw, Clear, partial draw: the full ordered triple inside ONE cycle.
    void RunDrawClearDraw(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O3 draw, Clear, draw")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "O3 draw, Clear, draw: the Clear() wipes only what preceded it and the "
                         "later draw lands on top of it");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "O3 draw, Clear, draw: the load-action clear runs before both draws, so "
                         "the first draw survives outside the second (declared defect)");
    }

    /// O4 -- two Clear()s with a draw between them: the second must wipe both.
    void RunMultipleClears(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O4 multiple Clears")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectUniform(p, kRT, kRT, kYellow,
                          "O4 multiple Clears: the second Clear() of a bind cycle happens, after "
                          "the draw that sits between them");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kYellow,
                         "O4 multiple Clears: only the last clear value reaches the pass load "
                         "action, and it runs before the draw (declared defect)");
    }

    /// O5 -- A -> B -> A clear values in one cycle: the last one wins and is not a stale A.
    void RunRepeatedClearValues(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O5 A->B->A clear values")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kMagenta, 1.0f, 0);
        dev.Clear(ClearOptions::Target, kCyan, 1.0f, 0);
        dev.Clear(ClearOptions::Target, kMagenta, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.clearOnPreserveTarget)
            ExpectUniform(p, kRT, kRT, kMagenta,
                          "O5 A->B->A clear values: three Clear()s in one cycle end at the last "
                          "requested colour");
        else
            check(!p.threw && !Same(p.dest[0], kMagenta) && !Same(p.dest[0], kCyan),
                  "O5 A->B->A clear values: every Clear() is dropped here (declared defect) -- "
                  "background=" + ColorText(p.dest[0]));
    }

    /// O6 -- the clear lands in a genuine SECOND bind segment, after a first cycle painted content.
    void RunClearInSecondSegment(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O6 Clear in a second segment")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.clearOnPreserveTarget)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "O6 Clear in a second segment: a Clear() in a later bind cycle of a "
                         "PreserveContents target replaces the earlier cycle's content");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "O6 Clear in a second segment: the Clear() is dropped and the first "
                         "cycle's content shows through (declared defect)");
    }

    /// O7 -- a bind cycle whose ONLY command is a Clear().
    void RunClearWithNoDraw(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("O7 Clear with no draw")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kCyan, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.clearOnPreserveTarget)
            ExpectUniform(p, kRT, kRT, kCyan,
                          "O7 Clear with no draw: a bind cycle containing nothing but a Clear() "
                          "still reaches the target");
        else
            ExpectUniform(p, kRT, kRT, kRed,
                          "O7 Clear with no draw: the clear-only cycle changes nothing here "
                          "(declared defect)");
    }

    // =====================================================================
    // U -- RenderTargetUsage must not decide whether Clear works
    // =====================================================================

    /// U1 -- DiscardContents: the bind-time discard AND the explicit ordered Clear both happen.
    void RunDiscardPlusClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("U1 Discard + Clear")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(rt.get());   // shared layer clears to opaque black here
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "U1 Discard + Clear: an ordered Clear() inside a DiscardContents cycle "
                         "wipes the draw before it");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "U1 Discard + Clear: the load-action clear runs first, so the draw "
                         "survives (declared defect)");
    }

    /// U2 -- PlatformContents preserves, and the Clear must still win over what it preserved.
    void RunPlatformPlusClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("U2 Platform + Clear")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PlatformContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.clearOnPreserveTarget)
            ExpectUniform(p, kRT, kRT, kYellow,
                          "U2 Platform + Clear: a Clear() overrides what PlatformContents "
                          "preserved");
        else
            ExpectUniform(p, kRT, kRT, kRed,
                          "U2 Platform + Clear: the Clear() is dropped (declared defect)");
    }

    /// U3 -- mixed targets A -> B -> A, each cycle clearing to its own colour.
    void RunMixedTargetsWithClears(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("U3 A->B->A with clears")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(a.get());
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        dev.SetRenderTarget(nullptr);
        dev.SetRenderTarget(b.get());
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTarget(nullptr);
        dev.SetRenderTarget(a.get());
        dev.Clear(ClearOptions::Target, kBlue, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        Probe pa = ReadTarget(*a);
        Probe pb = ReadTarget(*b);
        if (kContract.clearOnPreserveTarget)
        {
            ExpectUniform(pa, kRT, kRT, kBlue,
                          "U3 A->B->A with clears: the twice-bound target ends at its own final "
                          "cycle's clear colour");
            ExpectUniform(pb, kRT, kRT, kGreen,
                          "U3 A->B->A with clears: the interleaved target keeps its own clear "
                          "colour, not a neighbour's");
        }
        else
        {
            check(!pa.threw && !pb.threw && !Same(pa.dest[0], kBlue) && !Same(pb.dest[0], kGreen),
                  "U3 A->B->A with clears: every clear is dropped here (declared defect) -- a=" +
                      ColorText(pa.dest[0]) + " b=" + ColorText(pb.dest[0]));
        }
    }

    // =====================================================================
    // B -- the backbuffer
    // =====================================================================

    /// B1 -- backbuffer: draw, Clear, partial draw, all before the frame ends.
    void RunBackbufferOrderedClear(GraphicsDevice& dev)
    {
        if (!kContract.backbufferReadback)
        {
            skip("B1 backbuffer Clear: skipped -- this renderer cannot read the backbuffer");
            return;
        }
        FillBackbuffer(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillBackbufferLeft(kBlue);

        Probe p = ReadBackbuffer(dev);
        if (kContract.orderedClear)
            ExpectHalves(p, kBBW, kBBH, kBlue, kGreen,
                         "B1 backbuffer Clear: a Clear() after earlier backbuffer geometry wipes "
                         "it and the later geometry lands on top");
        else
            ExpectHalves(p, kBBW, kBBH, kBlue, kRed,
                         "B1 backbuffer Clear: the load-action clear runs before both draws "
                         "(declared defect)");
    }

    // =====================================================================
    // K -- RenderTargetCube
    // =====================================================================

    /// K1 -- a face Clear must reach that face and no other.
    void RunCubeFaceClear(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargets || !kContract.cubeReadback)
        {
            skip("K1 cube face Clear: skipped -- no exact RenderTargetCube readback here");
            return;
        }
        auto cube = std::make_unique<RenderTargetCube>(dev, kRT, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        FillTarget(kRed);
        dev.SetRenderTargets({});

        dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveY);
        FillTarget(kBlue);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        dev.SetRenderTargets({});

        Probe py = ReadFace(*cube, static_cast<int>(CubeMapFace::PositiveY));
        Probe px = ReadFace(*cube, static_cast<int>(CubeMapFace::PositiveX));
        if (kContract.orderedClear)
            ExpectUniform(py, kRT, kRT, kYellow,
                          "K1 cube face Clear: an ordered Clear() on a cube face wipes that face's "
                          "earlier draw");
        else
            ExpectUniform(py, kRT, kRT, kBlue,
                          "K1 cube face Clear: the load-action clear runs before the draw "
                          "(declared defect)");
        ExpectUniform(px, kRT, kRT, kRed,
                      "K1 cube face Clear: clearing one face leaves every other face alone");
    }

    // =====================================================================
    // M -- multiple render targets
    // =====================================================================

    /// M1 -- ClearOptions::Target must reach EVERY bound colour attachment.
    void RunMrtClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("M1 MRT Clear")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto outside = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(outside.get());
        dev.Clear(ClearOptions::Target, kMagenta, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        if (!TryBindMrt(dev, *a, *b))
        {
            skip("M1 MRT Clear: skipped -- this renderer refuses two simultaneous render targets");
            return;
        }
        // The 2D pipeline writes attachment 0 only, so this draw is what the clear must overwrite
        // on `a`; on `b` the clear is the only thing that ever writes it.
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTargets({});

        Probe pa = ReadTarget(*a);
        Probe pb = ReadTarget(*b);
        Probe po = ReadTarget(*outside);
        if (kContract.orderedClear)
            ExpectUniform(pa, kRT, kRT, kGreen,
                          "M1 MRT Clear: ClearOptions::Target reaches MRT attachment 0");
        else
            ExpectUniform(pa, kRT, kRT, kRed,
                          "M1 MRT Clear: the load-action clear runs before the draw on attachment "
                          "0 (declared defect)");
        ExpectUniform(pb, kRT, kRT, kGreen,
                      "M1 MRT Clear: ClearOptions::Target reaches every OTHER bound colour "
                      "attachment too");
        ExpectUniform(po, kRT, kRT, kMagenta,
                      "M1 MRT Clear: a target that is not part of the bound set is untouched");
    }

    // =====================================================================
    // D -- depth and stencil, proven by rendering
    // =====================================================================

    /**
     * @brief D1 -- ClearOptions::DepthBuffer alone.
     *
     * A near full-target quad writes colour and depth. A far LEFT quad is then rejected by that
     * depth. A depth-ONLY Clear() follows, and the same far LEFT quad must now be accepted -- while
     * the RIGHT half proves the colour attachment was NOT cleared with it.
     */
    void RunDepthOnlyClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D1 depth-only Clear")) return;
        if (!kContract.primitives3D || !kContract.depthBuffer3D)
        {
            skip("D1 depth-only Clear: skipped -- no depth-tested 3D draws on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kRed, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 1.0f, 0.25f, kCyan);   // near, whole target
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.75f, kBlue);   // far, LEFT -- must be rejected
        dev.Clear(ClearOptions::DepthBuffer, kWhite, 1.0f, 0);  // depth only; colour must survive
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.75f, kBlue);   // far, LEFT -- must now be accepted
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kCyan,
                         "D1 depth-only Clear: ClearOptions::DepthBuffer resets the depth buffer at "
                         "its call position and leaves the colour attachment alone");
        else
            ExpectUniform(p, kRT, kRT, kCyan,
                          "D1 depth-only Clear: a depth-only Clear() reaches nothing here, so the "
                          "far quad stays rejected (declared defect)");
    }

    /**
     * @brief D2 -- ClearOptions::Stencil alone.
     *
     * The LEFT half is stamped with stencil 1, then a stencil-only Clear() resets it to 0, then a
     * whole-target quad gated on stencil == 1 is drawn. With an ordered stencil clear it draws
     * nowhere; without one it repaints the left half.
     */
    void RunStencilOnlyClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D2 stencil-only Clear")) return;
        if (!kContract.primitives3D || !kContract.stencilBuffer3D)
        {
            skip("D2 stencil-only Clear: skipped -- stencil testing does not gate fragments here");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kRed, 1.0f, 0);
        Draw3D(dev, StencilWriteState(1), -1.0f, 0.0f, 0.5f, kGreen);  // LEFT: stencil <- 1
        dev.Clear(ClearOptions::Stencil, kWhite, 1.0f, 0);             // stencil only
        Draw3D(dev, StencilTestState(1), -1.0f, 1.0f, 0.5f, kBlue);    // gated on stencil == 1
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kGreen, kRed,
                         "D2 stencil-only Clear: ClearOptions::Stencil resets the stencil buffer at "
                         "its call position, so the gated quad draws nowhere, and the colour "
                         "attachment is untouched");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "D2 stencil-only Clear: the stencil clear reaches nothing, so the gated "
                         "quad still passes on the stamped half (declared defect)");
    }

    /// D3 -- Target | DepthBuffer together: both aspects, one call.
    void RunColourAndDepthClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D3 Target|DepthBuffer")) return;
        if (!kContract.primitives3D || !kContract.depthBuffer3D)
        {
            skip("D3 Target|DepthBuffer: skipped -- no depth-tested 3D draws on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kRed, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 1.0f, 0.25f, kCyan);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kGreen, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.75f, kBlue);  // far LEFT: accepted once depth reset
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "D3 Target|DepthBuffer: one call resets both the colour and the depth "
                         "buffer at its call position");
        else
            ExpectUniform(p, kRT, kRT, kCyan,
                          "D3 Target|DepthBuffer: neither aspect reaches the target after a draw "
                          "(declared defect)");
    }

    /// D4 -- Target | Stencil together, with depth left alone.
    void RunColourAndStencilClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D4 Target|Stencil")) return;
        if (!kContract.primitives3D || !kContract.stencilBuffer3D)
        {
            skip("D4 Target|Stencil: skipped -- stencil testing does not gate fragments here");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kRed, 1.0f, 0);
        Draw3D(dev, StencilWriteState(1), -1.0f, 0.0f, 0.5f, kGreen);
        dev.Clear(ClearOptions::Target | ClearOptions::Stencil, kYellow, 1.0f, 0);
        Draw3D(dev, StencilTestState(1), -1.0f, 1.0f, 0.5f, kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectUniform(p, kRT, kRT, kYellow,
                          "D4 Target|Stencil: the colour becomes the requested value and the gated "
                          "quad finds no matching stencil left");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "D4 Target|Stencil: neither aspect reaches the target after a draw "
                         "(declared defect)");
    }

    /// D5 -- DepthBuffer | Stencil together, with the colour attachment left alone.
    void RunDepthAndStencilClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D5 DepthBuffer|Stencil")) return;
        if (!kContract.primitives3D || !kContract.depthBuffer3D || !kContract.stencilBuffer3D)
        {
            skip("D5 DepthBuffer|Stencil: skipped -- no depth or stencil oracle on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kRed, 1.0f, 0);
        // Near, whole target, writing depth AND stencil = 1.
        {
            DepthStencilState ds = StencilWriteState(1);
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            ds.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
            Draw3D(dev, ds, -1.0f, 1.0f, 0.25f, kCyan);
        }
        dev.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, kWhite, 1.0f, 0);
        // LEFT: far, depth-tested only -- must be accepted now the depth is reset.
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.75f, kBlue);
        // RIGHT: gated on stencil == 1 -- must be rejected now the stencil is reset.
        Draw3D(dev, StencilTestState(1), 0.0f, 1.0f, 0.5f, kMagenta);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kCyan,
                         "D5 DepthBuffer|Stencil: both buffers reset at the call position and the "
                         "colour attachment keeps what was drawn into it");
        else
            ExpectHalves(p, kRT, kRT, kCyan, kMagenta,
                         "D5 DepthBuffer|Stencil: neither buffer is reset, so the far quad stays "
                         "rejected and the gated quad still passes (declared defect)");
    }

    /// D6 -- Target | DepthBuffer | Stencil, the full mask.
    void RunAllThreeClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D6 Target|DepthBuffer|Stencil")) return;
        if (!kContract.primitives3D || !kContract.depthBuffer3D || !kContract.stencilBuffer3D)
        {
            skip("D6 Target|DepthBuffer|Stencil: skipped -- no depth or stencil oracle here");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kRed, 1.0f, 0);
        {
            DepthStencilState ds = StencilWriteState(1);
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            ds.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
            Draw3D(dev, ds, -1.0f, 1.0f, 0.25f, kCyan);
        }
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kYellow, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.75f, kBlue);
        Draw3D(dev, StencilTestState(1), 0.0f, 1.0f, 0.5f, kMagenta);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kYellow,
                         "D6 Target|DepthBuffer|Stencil: all three aspects are reset at the call "
                         "position");
        else
            ExpectHalves(p, kRT, kRT, kCyan, kMagenta,
                         "D6 Target|DepthBuffer|Stencil: none of the three reaches the target after "
                         "a draw (declared defect)");
    }

    /// D7 -- the depth clear VALUE, at both ends of the legal range.
    void RunDepthClearValueBoundaries(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D7 depth clear value")) return;
        if (!kContract.primitives3D || !kContract.depthBuffer3D)
        {
            skip("D7 depth clear value: skipped -- no depth-tested 3D draws on this renderer");
            return;
        }
        auto near0 = MakeTarget(dev, RenderTargetUsage::PreserveContents,
                                DepthFormat::Depth24Stencil8);
        auto far1 = MakeTarget(dev, RenderTargetUsage::PreserveContents,
                               DepthFormat::Depth24Stencil8);

        // depth <- 0.0: NOTHING at z = 0.5 can pass LessEqual.
        dev.SetRenderTarget(near0.get());
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        dev.Clear(ClearOptions::DepthBuffer, kWhite, 0.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 1.0f, 0.5f, kBlue);
        dev.SetRenderTarget(nullptr);

        // depth <- 1.0: everything at z = 0.5 passes.
        dev.SetRenderTarget(far1.get());
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        dev.Clear(ClearOptions::DepthBuffer, kWhite, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 1.0f, 0.5f, kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p0 = ReadTarget(*near0);
        Probe p1 = ReadTarget(*far1);
        if (kContract.orderedClear)
        {
            ExpectUniform(p0, kRT, kRT, kRed,
                          "D7 depth clear value: a depth clear to 0.0 rejects everything farther");
            ExpectUniform(p1, kRT, kRT, kBlue,
                          "D7 depth clear value: a depth clear to 1.0 accepts everything nearer");
        }
        else
        {
            skip("D7 depth clear value: skipped -- a depth-only Clear() reaches nothing here "
                 "(declared defect)");
            skip("D7 depth clear value: skipped -- second half of the same declaration");
        }
    }

    /// D8 -- stencil clear VALUES 0, 1 and 255, each proven by a gate that must or must not pass.
    void RunStencilClearValues(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("D8 stencil clear values")) return;
        if (!kContract.primitives3D || !kContract.stencilBuffer3D)
        {
            skip("D8 stencil clear values: skipped -- stencil testing does not gate fragments here");
            return;
        }
        struct Case { int clearTo; int gate; bool passes; const char* text; };
        const std::array<Case, 4> cases = {{
            {0,   0,   true,  "0 gated on 0 passes"},
            {1,   1,   true,  "1 gated on 1 passes"},
            {255, 255, true,  "255 gated on 255 passes"},
            {1,   255, false, "1 gated on 255 does not pass"},
        }};
        for (const Case& c : cases)
        {
            auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
            // Stamp a value the clear must overwrite, so a no-op clear cannot pass by accident.
            Draw3D(dev, StencilWriteState(7), -1.0f, 1.0f, 0.5f, kRed);
            dev.Clear(ClearOptions::Stencil, kWhite, 1.0f, c.clearTo);
            Draw3D(dev, StencilTestState(c.gate), -1.0f, 1.0f, 0.5f, kGreen);
            dev.SetRenderTarget(nullptr);

            Probe p = ReadTarget(*rt);
            const std::string label = std::string("D8 stencil clear values: ") + c.text;
            if (kContract.orderedClear)
                ExpectUniform(p, kRT, kRT, c.passes ? kGreen : kRed, label);
            else
                skip(label + ": skipped -- a stencil-only Clear() reaches nothing here (declared "
                             "defect)");
        }
    }

    // =====================================================================
    // V -- viewport and scissor around Clear
    // =====================================================================

    /// V1 -- a custom sub-Viewport is active across the Clear.
    void RunClearUnderViewport(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("V1 Clear under a sub-viewport")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        const Viewport saved = dev.getViewportProperty();
        dev.setViewportProperty(Viewport(0, 0, kHalf, kHalf));
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.setViewportProperty(saved);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (!kContract.orderedClear)
        {
            ExpectUniform(p, kRT, kRT, kRed,
                          "V1 Clear under a sub-viewport: the Clear() is dropped entirely here "
                          "(declared defect)");
            return;
        }
        if (kContract.clearIgnoresViewport)
            ExpectUniform(p, kRT, kRT, kGreen,
                          "V1 Clear under a sub-viewport: Clear covers the whole target, "
                          "unrestricted by Viewport (REMED-GFX-018 contract)");
        else
            Expect(p, kRT, kRT,
                   [](int x, int y) { return (x < kHalf && y < kHalf) ? kGreen : kRed; },
                   "V1 Clear under a sub-viewport: this renderer scopes Clear to the Viewport "
                   "(declared divergence)");
    }

    /// V2 -- an enabled ScissorRectangle is active across the Clear.
    void RunClearUnderScissor(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("V2 Clear under a scissor")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);
        dev.setRasterizerStateProperty(scissored);
        const Rectangle savedScissor = dev.getScissorRectangleProperty();
        dev.setScissorRectangleProperty(Rectangle(0, 0, kHalf, kHalf));
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.setScissorRectangleProperty(savedScissor);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (!kContract.orderedClear)
        {
            ExpectUniform(p, kRT, kRT, kRed,
                          "V2 Clear under a scissor: the Clear() is dropped entirely here "
                          "(declared defect)");
            return;
        }
        if (kContract.clearIgnoresScissor)
            ExpectUniform(p, kRT, kRT, kGreen,
                          "V2 Clear under a scissor: Clear covers the whole target, unrestricted "
                          "by ScissorRectangle (REMED-GFX-018 contract)");
        else
            Expect(p, kRT, kRT,
                   [](int x, int y) { return (x < kHalf && y < kHalf) ? kGreen : kRed; },
                   "V2 Clear under a scissor: this renderer scopes Clear to the scissor rectangle "
                   "(declared divergence)");
    }

    // =====================================================================
    // S -- multisample targets
    // =====================================================================

    /// S1 -- an ordered Clear() on a multisampled target must reach the RESOLVED surface.
    void RunMsaaOrderedClear(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("S1 MSAA ordered Clear")) return;
        if (!kContract.msaaTargetReadback)
        {
            skip("S1 MSAA ordered Clear: skipped -- a multisampled target does not read back its "
                 "resolved content here");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents, DepthFormat::None,
                             kMsaaRequest);
        const int applied = rt->getMultiSampleCountProperty();
        if (applied <= 1)
        {
            skip("S1 MSAA ordered Clear: skipped -- the device applied no multisampling (requested "
                 + std::to_string(kMsaaRequest) + ", applied " + std::to_string(applied) + ")");
            return;
        }
        dev.SetRenderTarget(rt.get());
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kBlue);
        dev.SetRenderTarget(nullptr);

        Probe p = ReadTarget(*rt);
        if (kContract.orderedClear)
            ExpectHalves(p, kRT, kRT, kBlue, kGreen,
                         "S1 MSAA ordered Clear: the resolved surface reflects the ordered Clear "
                         "and the draw that followed it (applied " + std::to_string(applied) +
                             "x)");
        else
            ExpectHalves(p, kRT, kRT, kBlue, kRed,
                         "S1 MSAA ordered Clear: the load-action clear runs before both draws "
                         "(declared defect)");
    }

    // =====================================================================
    // R -- resource cardinality: many Clears must not build anything up
    // =====================================================================

    // =====================================================================
    // P -- REMED-GFX-156: pass state must survive an ordered Clear boundary
    // =====================================================================

    /**
     * @brief The parity oracle every P check uses.
     *
     * A renderer that delivers an ordered mid-cycle Clear by ending its native render pass and
     * beginning another one has to reinstate, for the second pass, every piece of pass state the
     * first pass's end discarded. A pixel oracle that only asks "did the clear happen" is blind to
     * all of it, so each check runs the SAME state-sensitive draw twice: once at the head of a bind
     * cycle (control, one native pass) and once after a full-target draw that an ordered Clear
     * erases (subject, two native passes). The two readbacks must be byte-identical.
     *
     * @param dev The device.
     * @param label What the check is called.
     * @param draw Issues the state-sensitive draw. Called once per leg, in both arrangements.
     */
    template <typename Fn>
    void ExpectClearBoundaryParity(GraphicsDevice& dev, const std::string& label, Fn draw)
    {
        if (!NeedRtReadback(label)) return;
        auto control = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(control.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        draw(dev);
        dev.SetRenderTarget(nullptr);
        Probe ctrl = ReadTarget(*control);
        if (ctrl.threw) { check(false, label + " [control readback threw: " + ctrl.what + "]"); return; }

        auto subject = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(subject.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        FillTarget(kMagenta);                      // everything the ordered Clear must erase
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  kBlack, 1.0f, 0);
        draw(dev);
        dev.SetRenderTarget(nullptr);
        Probe subj = ReadTarget(*subject);
        if (subj.threw) { check(false, label + " [subject readback threw: " + subj.what + "]"); return; }

        Expect(subj, kRT, kRT,
               [&](int x, int y) { return ctrl.dest[static_cast<std::size_t>(y) * kRT + x]; },
               label);
    }

    /// P1 -- the Viewport a draw was queued under still applies in the pass the Clear opened.
    void RunViewportAcrossClearBoundary(GraphicsDevice& dev)
    {
        ExpectClearBoundaryParity(
            dev, "P1 Viewport survives a Clear boundary",
            [this](GraphicsDevice& d)
            {
                const Viewport saved = d.getViewportProperty();
                d.setViewportProperty(Viewport(0, 0, kHalf, kHalf));
                Fill(kGreen, 0, 0, kRT, kRT);
                d.setViewportProperty(saved);
            });
    }

    /// P2 -- the ScissorRectangle a draw was queued under still applies after the boundary.
    void RunScissorAcrossClearBoundary(GraphicsDevice& dev)
    {
        ExpectClearBoundaryParity(
            dev, "P2 ScissorRectangle survives a Clear boundary",
            [this](GraphicsDevice& d)
            {
                RasterizerState scissored = RasterizerState::CullNone;
                scissored.setScissorTestEnableProperty(true);
                const Rectangle saved = d.getScissorRectangleProperty();
                d.setScissorRectangleProperty(Rectangle(0, 0, kHalf, kRT));
                SamplerState point = SamplerState::PointClamp;
                DepthStencilState noDepth = DepthStencilState::None;
                sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth,
                           &scissored);
                sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), kGreen);
                sb_->End();
                d.setScissorRectangleProperty(saved);
                d.setRasterizerStateProperty(RasterizerState::CullNone);
            });
    }

    /// P3 -- the blend constant (GraphicsDevice.BlendFactor) still reaches the reopened pass.
    void RunBlendFactorAcrossClearBoundary(GraphicsDevice& dev)
    {
#if defined(CNA_RENDERER_LLGL)
        bool rejected = false;
        try
        {
            BlendState factored;
            factored.setColorSourceBlendProperty(Blend::BlendFactor);
            dev.setBlendStateProperty(factored);
        }
        catch (const System::NotSupportedException&)
        {
            rejected = true;
        }
        check(rejected,
              "P3 BlendFactor: LLGL OpenGL rejects the unsupported constant-blend-factor route "
              "deterministically");
        return;
#endif

        ExpectClearBoundaryParity(
            dev, "P3 BlendFactor survives a Clear boundary",
            [this](GraphicsDevice& d)
            {
                // Opaque white under SourceBlend=BlendFactor resolves to the blend constant itself,
                // so the drawn colour IS the piece of pass state under test.
                BlendState factored;
                factored.setColorSourceBlendProperty(Blend::BlendFactor);
                factored.setColorDestinationBlendProperty(Blend::Zero);
                factored.setAlphaSourceBlendProperty(Blend::One);
                factored.setAlphaDestinationBlendProperty(Blend::Zero);
                const Color savedFactor = d.getBlendFactorProperty();
                d.setBlendFactorProperty(kCyan);
                SamplerState point = SamplerState::PointClamp;
                DepthStencilState noDepth = DepthStencilState::None;
                RasterizerState noCull = RasterizerState::CullNone;
                sb_->Begin(SpriteSortMode::Deferred, factored, &point, &noDepth, &noCull);
                sb_->Draw(*white_, Rectangle(0, 0, kHalf, kRT), Rectangle(0, 0, 1, 1), kWhite);
                sb_->End();
                d.setBlendFactorProperty(savedFactor);
            });
    }

    /// P4 -- the depth buffer and depth state still gate the draw after the boundary.
    void RunDepthStateAcrossClearBoundary(GraphicsDevice& dev)
    {
        if (!kContract.primitives3D || !kContract.depthBuffer3D)
        {
            skip("P4 depth state survives a Clear boundary: skipped -- no depth oracle here");
            return;
        }
        ExpectClearBoundaryParity(
            dev, "P4 depth state survives a Clear boundary",
            [this](GraphicsDevice& d)
            {
                Draw3D(d, DepthState(), -1.0f, 1.0f, 0.3f, kBlue);   // near, writes depth
                Draw3D(d, DepthState(), -1.0f, 0.0f, 0.7f, kRed);    // farther, must be rejected
            });
    }

    /// P5 -- the stencil reference and stencil buffer still gate the draw after the boundary.
    void RunStencilAcrossClearBoundary(GraphicsDevice& dev)
    {
        if (!kContract.primitives3D || !kContract.stencilBuffer3D)
        {
            skip("P5 stencil state survives a Clear boundary: skipped -- stencil testing does not "
                 "gate fragments here");
            return;
        }
        ExpectClearBoundaryParity(
            dev, "P5 stencil state survives a Clear boundary",
            [this](GraphicsDevice& d)
            {
                Draw3D(d, StencilWriteState(7), -1.0f, 0.0f, 0.5f, kBlue);  // left half writes 7
                Draw3D(d, StencilTestState(7), -1.0f, 1.0f, 0.5f, kGreen);  // only where 7
            });
    }

    // =====================================================================
    // T -- REMED-GFX-156: destination transitions around ordered Clears
    // =====================================================================

    /// T1 -- backbuffer -> target -> backbuffer, each cycle drawing then clearing.
    void RunBackbufferTargetBackbuffer(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("T1 backbuffer -> target -> backbuffer")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        FillBackbuffer(kRed);
        dev.Clear(ClearOptions::Target, kBlue, 1.0f, 0);

        dev.SetRenderTarget(rt.get());
        FillTarget(kMagenta);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        FillLeftHalf(kYellow);
        dev.SetRenderTarget(nullptr);

        FillBackbufferLeft(kCyan);

        Probe pt = ReadTarget(*rt);
        ExpectHalves(pt, kRT, kRT, kYellow, kGreen,
                     "T1 backbuffer -> target -> backbuffer: the target cycle's own ordered Clear "
                     "wipes only its own earlier draw");
        if (!kContract.backbufferReadback)
        {
            skip("T1 backbuffer -> target -> backbuffer: backbuffer half skipped -- no readback");
            return;
        }
        Probe pb = ReadBackbuffer(dev);
        ExpectHalves(pb, kBBW, kBBH, kCyan, kBlue,
                     "T1 backbuffer -> target -> backbuffer: the backbuffer's ordered Clear wiped "
                     "its first draw, and the draw issued after the target cycle lands on top");
    }

    /// T2 -- two equal-sized targets, each with its own draw-then-Clear, interleaved.
    void RunTwoEqualTargets(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("T2 two equal-sized targets")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(a.get());
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(b.get());
        FillTarget(kBlue);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        FillLeftHalf(kMagenta);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(a.get());
        FillLeftHalf(kCyan);
        dev.SetRenderTarget(nullptr);

        ExpectHalves(ReadTarget(*a), kRT, kRT, kCyan, kGreen,
                     "T2 two equal-sized targets: A keeps its own ordered Clear and the later "
                     "cycle's draw composes with it");
        ExpectHalves(ReadTarget(*b), kRT, kRT, kMagenta, kYellow,
                     "T2 two equal-sized targets: B keeps its own ordered Clear, not A's");
    }

    /// T3 -- two DIFFERENTLY sized targets: a boundary must not carry the other's extent.
    void RunTwoDifferentlySizedTargets(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("T3 two differently sized targets")) return;
        auto small = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        RenderTarget2D big(dev, kRT * 2, kRT * 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(&big);
        Fill(kRed, 0, 0, kRT * 2, kRT * 2);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        Fill(kBlue, 0, 0, kRT, kRT * 2);
        dev.SetRenderTarget(nullptr);

        dev.SetRenderTarget(small.get());
        FillTarget(kMagenta);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        std::vector<Color> bigPixels(static_cast<std::size_t>(kRT * 2) * (kRT * 2), Sentinel());
        bool threw = false;
        std::string what;
        try { big.GetData(0, nullptr, bigPixels.data(), 0, static_cast<int>(bigPixels.size())); }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        if (threw)
        {
            check(false, "T3 two differently sized targets [readback threw: " + what + "]");
        }
        else
        {
            std::size_t bad = 0;
            for (int y = 0; y < kRT * 2; ++y)
                for (int x = 0; x < kRT * 2; ++x)
                {
                    const Color want = x < kRT ? kBlue : kGreen;
                    if (!Same(bigPixels[static_cast<std::size_t>(y) * (kRT * 2) + x], want)) ++bad;
                }
            check(bad == 0, "T3 two differently sized targets: the larger target's ordered Clear "
                            "covers ITS whole extent, not the smaller one's [mismatched=" +
                                std::to_string(bad) + "/" +
                                std::to_string(static_cast<std::size_t>(kRT * 2) * (kRT * 2)) + "]");
        }
        ExpectUniform(ReadTarget(*small), kRT, kRT, kYellow,
                      "T3 two differently sized targets: the smaller target's own ordered Clear "
                      "wipes its own draw");
    }

    /// T4 -- an ordered Clear on a SECOND cube face leaves the first face's clear standing.
    void RunSecondCubeFaceClear(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargets || !kContract.cubeReadback)
        {
            skip("T4 second cube face Clear: skipped -- no exact RenderTargetCube readback here");
            return;
        }
        auto cube = std::make_unique<RenderTargetCube>(dev, kRT, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(cube.get(), CubeMapFace::NegativeX);
        FillTarget(kRed);
        dev.Clear(ClearOptions::Target, kGreen, 1.0f, 0);
        dev.SetRenderTargets({});

        dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveZ);
        FillTarget(kBlue);
        dev.Clear(ClearOptions::Target, kYellow, 1.0f, 0);
        FillLeftHalf(kMagenta);
        dev.SetRenderTargets({});

        ExpectUniform(ReadFace(*cube, static_cast<int>(CubeMapFace::NegativeX)), kRT, kRT, kGreen,
                      "T4 second cube face Clear: the first face keeps its own ordered Clear");
        ExpectHalves(ReadFace(*cube, static_cast<int>(CubeMapFace::PositiveZ)), kRT, kRT,
                     kMagenta, kYellow,
                     "T4 second cube face Clear: the second face's ordered Clear wipes only that "
                     "face's earlier draw");
        // A face neither cycle ever bound has never been rendered into, so what it holds is this
        // renderer's own uninitialized surface -- not something to predict. What IS asserted is the
        // claim: neither ordered Clear reached it. K1 already asserts the positive form for a face
        // that WAS painted.
        Probe untouched = ReadFace(*cube, static_cast<int>(CubeMapFace::PositiveY));
        bool leaked = untouched.threw;
        for (const Color& c : untouched.dest)
            if (Same(c, kGreen) || Same(c, kYellow) || Same(c, kMagenta)) { leaked = true; break; }
        check(!leaked, "T4 second cube face Clear: a face neither cycle bound is reached by "
                       "neither ordered Clear (got " + ColorText(untouched.dest[0]) + ")");
    }

    /// A1 -- consecutive Clears naming DIFFERENT aspects must compose, not replace each other.
    void RunAspectCoalescing(GraphicsDevice& dev)
    {
        if (!kContract.primitives3D || !kContract.depthBuffer3D)
        {
            skip("A1 aspect coalescing: skipped -- no depth oracle here");
            return;
        }
        if (!NeedRtReadback("A1 aspect coalescing")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBlack, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 1.0f, 0.3f, kBlue);   // near, writes depth
        // Two ordered Clears, no draw between them: colour red, then depth back to far. Both must
        // reach the SAME reopened pass -- a renderer that let the second replace the first would
        // leave either the colour uncleared or the depth uncleared.
        dev.Clear(ClearOptions::Target, kRed, 1.0f, 0);
        dev.Clear(ClearOptions::DepthBuffer, kWhite, 1.0f, 0);
        Draw3D(dev, DepthState(), -1.0f, 0.0f, 0.7f, kGreen);  // farther, now accepted
        dev.SetRenderTarget(nullptr);

        ExpectHalves(ReadTarget(*rt), kRT, kRT, kGreen, kRed,
                     "A1 aspect coalescing: Clear(Target) then Clear(DepthBuffer) with no draw "
                     "between them delivers BOTH -- the colour is the requested red and the "
                     "farther quad is accepted by the reset depth");
    }

    /// R1 -- 32 Clear()s in ONE bind cycle, then 8 cycles of 4 Clear()s each.
    void RunManyClears(GraphicsDevice& dev)
    {
        if (!NeedRtReadback("R1 many Clears")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        for (int i = 0; i < 32; ++i)
            dev.Clear(ClearOptions::Target, (i % 2 == 0) ? kRed : kBlue, 1.0f, 0);
        dev.Clear(ClearOptions::Target, kCyan, 1.0f, 0);
        dev.SetRenderTarget(nullptr);

        auto rt2 = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            dev.SetRenderTarget(rt2.get());
            for (int i = 0; i < 4; ++i)
                dev.Clear(ClearOptions::Target, (i % 2 == 0) ? kGreen : kYellow, 1.0f, 0);
            dev.SetRenderTarget(nullptr);
        }

        Probe p = ReadTarget(*rt);
        Probe p2 = ReadTarget(*rt2);
        if (kContract.clearOnPreserveTarget)
        {
            ExpectUniform(p, kRT, kRT, kCyan,
                          "R1 many Clears: 33 Clear()s in one bind cycle end at the last one");
            ExpectUniform(p2, kRT, kRT, kYellow,
                          "R1 many Clears: 8 cycles x 4 Clear()s end at the last cycle's last one");
        }
        else
        {
            check(!p.threw && !p2.threw && !Same(p.dest[0], kCyan) && !Same(p2.dest[0], kYellow),
                  "R1 many Clears: every clear is dropped here (declared defect) -- a=" +
                      ColorText(p.dest[0]) + " b=" + ColorText(p2.dest[0]));
        }
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        if (kContract.preferMultiSampling)
            gdm_->setPreferMultiSamplingProperty(true);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;
    }

    void Draw(const GameTime&) override
    {
        // Frame 0 is a warm-up: bgfx copies every createTexture payload at bgfx::frame(), so
        // without it the 1x1 source would still be unuploaded during the first real Draw.
        if (phase_ == 0)
        {
            phase_ = 1;
            FillBackbuffer(kBlack);
            return;
        }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        std::printf("REMED-GFX-129 ordered GraphicsDevice::Clear -- renderer %s\n", kContract.name);

        RunDrawThenClearSingleCycle(dev);
        RunClearThenPartialDraw(dev);
        RunDrawClearDraw(dev);
        RunMultipleClears(dev);
        RunRepeatedClearValues(dev);
        RunClearInSecondSegment(dev);
        RunClearWithNoDraw(dev);
        RunDiscardPlusClear(dev);
        RunPlatformPlusClear(dev);
        RunMixedTargetsWithClears(dev);
        RunCubeFaceClear(dev);
        RunMrtClear(dev);
        RunDepthOnlyClear(dev);
        RunStencilOnlyClear(dev);
        RunColourAndDepthClear(dev);
        RunColourAndStencilClear(dev);
        RunDepthAndStencilClear(dev);
        RunAllThreeClear(dev);
        RunDepthClearValueBoundaries(dev);
        RunStencilClearValues(dev);
        RunClearUnderViewport(dev);
        RunClearUnderScissor(dev);
        RunMsaaOrderedClear(dev);
        // REMED-GFX-156: pass state across the boundary an ordered Clear creates, and the
        // destination arrangements that boundary has to stay correct in.
        RunViewportAcrossClearBoundary(dev);
        RunScissorAcrossClearBoundary(dev);
        RunBlendFactorAcrossClearBoundary(dev);
        RunDepthStateAcrossClearBoundary(dev);
        RunStencilAcrossClearBoundary(dev);
        RunBackbufferTargetBackbuffer(dev);
        RunTwoEqualTargets(dev);
        RunTwoDifferentlySizedTargets(dev);
        RunSecondCubeFaceClear(dev);
        RunAspectCoalescing(dev);
        RunManyClears(dev);
        // Last, because it is the only check that leaves content on the backbuffer.
        RunBackbufferOrderedClear(dev);

        std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    OrderedClearTest test;
    test.Run();
    return test.Result();
}
