// SPDX-License-Identifier: MS-PL
// REMED-GFX-140: every public render-target bind/unbind cycle is its own logical pass.
//
// A deferred renderer may delay native submission for as long as it likes, but it must not merge
// two logically distinct bind cycles into one native pass when the two cycles differ in load
// action, clear operation, viewport, scissor or ordering. The sequence
//
//     SetRenderTarget(A); Draw(...); SetRenderTarget(nullptr);
//     SetRenderTarget(A); Draw(...); SetRenderTarget(nullptr);
//
// must NOT become equivalent to
//
//     SetRenderTarget(A); Draw(...); Draw(...); SetRenderTarget(nullptr);
//
// `VulkanRenderer::RecordCommandBuffer` did exactly that: its Phase 1 collected ONE entry
// per unique `VulkanRTSource` and replayed every queued sprite batch and 3D draw for that source
// inside a single `vkCmdBeginRenderPass`/`vkCmdEndRenderPass`. Two bind cycles of one target in
// one flush window therefore shared one load action.
//
// Why this file's decisive checks all use DiscardContents
// -------------------------------------------------------
// Collapsing is INVISIBLE on a PreserveContents target: one LOAD pass containing both cycles'
// draws produces exactly the same pixels as two LOAD passes. That is precisely why REMED-GFX-136
// needed an artificial readback barrier (`Settle()`) before eleven of its preservation checks
// could see a discarding renderer at all. The shape that cannot be faked is a target whose SECOND
// bind is supposed to throw the first bind's content away:
//
//     RT d(DiscardContents)
//     bind d; paint the whole target;  unbind      // shared layer clears to (0,0,0,255) at bind
//     bind d; paint a 2x2 marker only; unbind      // ... and must clear AGAIN here
//     read d                                       // -> black everywhere but the marker
//
// A renderer that collapses the two cycles clears once, then runs both draws, and returns the full
// pattern with a marker on top. No flush, no readback and no Present sits between the two cycles:
// the ONLY readback in each check happens after every command of that check is queued.
//
// What each check may and may not assume
// --------------------------------------
//   * DiscardContents -- CNA defines the replacement colour: GraphicsDevice::SetRenderTarget
//     clears a DiscardContents target to (0,0,0,255) on every bind, mirroring FNA. So "discarded"
//     is asserted as an exact colour, never as "anything but the old content".
//   * PreserveContents / PlatformContents -- the colour survives a full unbind/rebind cycle
//     exactly (REMED-GFX-136's contract, `usage != DiscardContents`).
//   * Depth/stencil -- its own contract, established by REMED-GFX-142 and enforced by
//     `examples/rendertarget_depthstencil_usage_test`. Nothing here asserts depth.
//   * An explicit Clear() inside a bind cycle -- REMED-GFX-129 records that Vulkan drops it on a
//     PreserveContents target, because the clear colour is delivered ONLY through the render
//     pass's load op and there is no vkCmdClearAttachments path. That is a SEPARATE defect from
//     pass collapsing, so this file declares it per renderer (`clearOnPreserveTarget`) and asserts
//     the declared behaviour either way -- checks X1/X2 are the exact GFX-129 reproduction, kept
//     here so GFX-140's fix can be re-measured against it rather than assumed to have covered it.
//   * Clear ordering inside ONE cycle -- same root cause: a load-op-only clear necessarily runs
//     before every draw of its pass, so "draw, then Clear, then draw" cannot wipe the first draw.
//     Declared as `clearAfterDrawWins` and asserted, not glossed over.
//
// The palette is 0/255-only, an exact fixed point of sRGB encoding, so every comparison in this
// file is byte-exact on every renderer including sRGB ones.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW    = 64;  ///< Backbuffer width.
    constexpr int kBBH    = 64;  ///< Backbuffer height.
    constexpr int kRT     = 8;   ///< Render-target edge (2D and cube).
    constexpr int kCorner = 3;   ///< Edge of each corner region in the producer pattern.
    constexpr int kMark   = 2;   ///< Edge of the small second-cycle marker.
    constexpr int kMsaaRequest = 4;  ///< Multisample count requested by the MSAA check.

    /**
     * @brief What a rendered render target's public readback must do on this renderer.
     *
     * `Exact` -- GetData returns the rendered surface byte for byte.
     * `Unsupported` -- GetData raises System::NotSupportedException with the caller's destination
     * untouched (REMED-GFX-127/130's contract).
     */
    enum class Support
    {
        Exact,
        Unsupported,
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        bool    rt2dTargets;   ///< RenderTarget2D can be created AND bound here.
        Support readback;      ///< RenderTarget2D::GetData at level 0.
        bool    cubeTargets;   ///< RenderTargetCube can be created AND bound here.
        Support cubeReadback;  ///< RenderTargetCube::GetData at level 0.
        /**
         * REMED-GFX-129: an explicit Clear() issued while a PreserveContents target is bound is
         * honoured. False is a declared, still-open defect -- Vulkan delivers the clear colour
         * only through the render pass load op, which a preserving pass sets to LOAD.
         */
        bool    clearOnPreserveTarget;
        /**
         * A Clear() issued AFTER a draw inside the SAME bind cycle wipes that draw, per XNA's
         * command order. False on a renderer whose only clear mechanism is the pass load action.
         */
        bool    clearAfterDrawWins;
        /**
         * Ask for a multisampled BACKBUFFER. Only true where that is the sole way a render target
         * can engage MSAA at all: Vulkan gates `VulkanRenderTargetRenderer`'s MSAA resources on the
         * renderer's own `sampleCount_`, so `multiSampleCount=4` on a target reports 0 unless the
         * device itself was created multisampled. Where this is true, check K1 REQUIRES the target
         * to come back with a real applied sample count -- an MSAA check that silently degraded to
         * single-sample would be a false positive.
         */
        bool    preferMultiSampling;
        /**
         * REMED-GFX-140's own subject: every public bind/unbind cycle becomes its own logical pass
         * here. False is a declared, still-open defect on that renderer (recorded with its own
         * finding); check S1 then asserts the COLLAPSED result, so it stays falsifiable in both
         * directions and turns red the day the renderer is fixed, and every other check that can
         * only distinguish the two shapes reports a skip naming the reason rather than a failure
         * that belongs to a different task.
         */
        bool    segmentsBindCycles;
        /**
         * A SpriteBatch fill honours a custom sub-Viewport, i.e. sprite coordinates are
         * VIEWPORT-LOCAL (FNA `SpriteBatch.PrepRenderState` builds its ortho from
         * `Viewport.Width/Height`). False where the fill covers the whole target regardless; that
         * is a pre-existing viewport defect on that renderer, not a pass-boundary one, so check S10
         * asserts the whole-target result there instead of failing for the wrong reason.
         */
        bool    spriteViewportIsLocal;
        /// A SpriteBatch fill honours GraphicsDevice.ScissorRectangle. Same rationale as above.
        bool    spriteScissorApplies;
        /**
         * A multisampled RenderTarget2D reads back its RESOLVED content. False where the readback
         * reports success over untouched memory (bgfx's blit from a multisampled attachment, the
         * same boundary REMED-GFX-134/138 recorded for cubes), which makes K1/K2 unable to say
         * anything about pass boundaries.
         */
        bool    msaaTargetReadback;
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing, so it owns no colour to preserve or discard and its readback is
    // REMED-GFX-127/130's deterministic refusal. Every sequence must still be legal.
    constexpr Contract kContract{"HEADLESS", true, Support::Unsupported, true, Support::Unsupported,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr Contract kContract{"SOFTWARE", true, Support::Exact, false, Support::Unsupported,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_EASYGL)
    constexpr Contract kContract{"EASYGL", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_BGFX)
    // `msaaTargetReadback` was false while a multisampled RenderTarget2D reported a successful
    // readback over untouched memory; REMED-GFX-154 fixed that, so K1/K2 measure pass boundaries on
    // a multisampled destination instead of skipping them.
    constexpr Contract kContract{"BGFX", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, false, true, true, false};
#elif defined(CNA_RENDERER_VULKAN)
    // `clearOnPreserveTarget` / `clearAfterDrawWins` were BOTH false here while REMED-GFX-129 was
    // open: `GetOrCreateRTRenderPass` delivered the clear colour through VK_ATTACHMENT_LOAD_OP_CLEAR
    // only, and a PreserveContents target's pass uses LOAD_OP_LOAD. REMED-GFX-129 replaced that with
    // real ordered `vkCmdClearAttachments` commands, so both are true and checks X1/X2/X3 now assert
    // the correct behaviour. They are kept here, unchanged in shape, because they are the exact
    // reproduction the finding was narrowed with.
    constexpr Contract kContract{"VULKAN", true, Support::Exact, true, Support::Exact,
                                 true, true, true, true, true, true, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `clearAfterDrawWins` was false while REMED-GFX-156 was open: wgpu delivers a clear colour
    // only through the render-pass load op, so a Clear() issued after a draw inside ONE cycle could
    // not wipe that draw (X3). REMED-GFX-156 put Clear into the ordered stream REMED-GFX-159 built
    // and cuts the cycle into one native pass per observable Clear; X3 was measured red the moment
    // the fix landed, which is what turned this declaration over.
    constexpr Contract kContract{"WEBGPU", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // `segmentsBindCycles` true since REMED-GFX-145. SdlGpu collapsed bind cycles exactly as Vulkan
    // did and for the same reason -- `EnsureFrameRendered` gave each DISTINCT `DrawTarget` one
    // `SDL_BeginGPURenderPass` holding every draw ever queued against it that frame -- and declared
    // false here until it was fixed. `clearAfterDrawWins` was false while REMED-GFX-156 was open:
    // SDL_gpu delivers a clear colour only through `SDL_GPUColorTargetInfo.load_op`, so a `Clear()`
    // issued after a draw inside ONE cycle could not wipe that draw (X3). REMED-GFX-156 made the
    // logical SEGMENT rather than the bind cycle own a load action, so an observable Clear opens
    // another segment over the same destination and X3 asserts the ordered result; it was measured
    // red the moment the fix landed. `clearOnPreserveTarget` is true, and X2 proves it holds in a
    // SECOND cycle too, because that cycle is a real second pass with its own load op.
    constexpr Contract kContract{"SDL_GPU", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr Contract kContract{"SDL_RENDERER", true, Support::Exact, false, Support::Unsupported,
                                 true, true, false, true, false, true, true, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", true, Support::Exact, false, Support::Unsupported,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", true, Support::Exact, false, Support::Unsupported,
                                 true, true, false, true, false, false, true, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr Contract kContract{"DIRECTX9", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, true, false};
#elif defined(CNA_RENDERER_SOKOL)
    // plans/plan_sokol.md SOKOL-25/26/38: both RenderTarget2D and RenderTargetCube can be created and
    // bound, and GetData() now works on both single-sample targets (a throwaway GL FBO around the
    // raw GL texture sg_gl_query_image_info() exposes), so readback/cubeReadback=Exact and every
    // single-sample readback-driven check here measures real pixels. `msaaTargetReadback` stays
    // false, conservatively: RunMsaaCycles's own skip guard (`readback != Exact ||
    // !msaaTargetReadback`) keeps K1/K2 skipped regardless, because a multisampled RenderTarget2D's
    // own multisample colour image has a DONTCARE store action at every pass end (SOKOL-26's
    // resolve-then-discard design), so whether its content survives a PreserveContents
    // unbind/rebind cycle the way K1/K2 require is genuinely unverified, not just untested. This
    // file's other real value for SOKOL is its non-readback checks (pass segmentation, sprite
    // viewport/scissor locality, and the cube-face bind cycle itself). Each public bind/unbind
    // cycle is genuinely its own sokol pass (BeginPassIfNeeded/EndPassIfActive), and an explicit
    // Clear() -- on a preserving target or after a draw in the same cycle -- always closes the
    // active pass so the next one starts with a real clear load action, matching FNA's own
    // command-ordered semantics exactly.
    constexpr Contract kContract{"SOKOL", true, Support::Exact, true, Support::Exact,
                                 true, true, false, true, true, true, false, false};
#else
#error "REMED-GFX-140: this renderer has no declared render-target pass-boundary contract."
#endif

    /// Destination pre-fill. Equals no pattern colour, no marker colour and not the discard colour.
    Color Sentinel() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }

    /// The colour GraphicsDevice::SetRenderTarget(s) clears a DiscardContents target to on bind.
    Color DiscardColour() { return Color(0, 0, 0, 255); }

    /// Fully saturated, so sRGB encoding is the identity on every asserted texel.
    const std::array<Color, 6> kPalette = {
        Color(255,   0,   0, 255),  // red
        Color(  0, 255,   0, 255),  // green
        Color(  0,   0, 255, 255),  // blue
        Color(255, 255,   0, 255),  // yellow
        Color(255,   0, 255, 255),  // magenta
        Color(  0, 255, 255, 255),  // cyan
    };

    /**
     * @brief Colour of pattern slot @p slot in producer round @p variant.
     *
     * Slot 0 is the base fill, 1..4 the corner regions, 5 the centre. `variant` rotates the
     * palette so two rounds of the producer share no colour in any slot -- a stale surface, a
     * surface belonging to another target and a surface from another round all differ at EVERY
     * asserted texel, not just at one.
     */
    Color Slot(int slot, int variant)
    {
        return kPalette[static_cast<std::size_t>((slot + 2 * variant) % 6)];
    }

    /// The exact texel the pattern producer puts at (@p x, @p y) in round @p variant.
    Color PatternTexel(int x, int y, int variant)
    {
        const int lo = kCorner;
        const int hi = kRT - kCorner;
        if (x >= lo && x < hi && y >= lo && y < hi) return Slot(5, variant);
        if (x < lo  && y < lo)  return Slot(1, variant);
        if (x >= hi && y < lo)  return Slot(2, variant);
        if (x < lo  && y >= hi) return Slot(3, variant);
        if (x >= hi && y >= hi) return Slot(4, variant);
        return Slot(0, variant);
    }

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

    /// One classified readback attempt: what the call did, in full, without judging it yet.
    struct Probe
    {
        bool threwNotSupported  = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> dest;
        std::size_t sentinelSurvivors = 0;
        std::size_t exact = 0;
        std::size_t window = 0;
        std::string firstMismatch;
    };
}

class RenderTargetPassBoundaryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::array<std::unique_ptr<Texture2D>, 6> solids_;
    /// P-series targets: queued in the battery frame, read only in the frame AFTER Present.
    std::unique_ptr<RenderTarget2D> pa_, pb_, pc_, pd_, pe_;
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
     * @brief Gate for a check that can only distinguish a real pass boundary from a collapsed one.
     *
     * On a renderer that declares `segmentsBindCycles == false` such a check would fail for a defect
     * that belongs to that renderer's own finding, not to this file's subject. Check S1 still runs
     * there and asserts the collapsed shape, so the declaration remains falsifiable.
     */
    bool RequireSegments(const std::string& label)
    {
        if (kContract.segmentsBindCycles) return true;
        skip(label + ": skipped -- this renderer does not give every public bind cycle its own "
                     "logical pass (declared open defect)");
        return false;
    }

    // ---------------------------------------------------------------------
    // Content producer -- drawn geometry only, never Clear()
    // ---------------------------------------------------------------------

    Texture2D& SolidFor(const Color& c)
    {
        for (std::size_t i = 0; i < kPalette.size(); ++i)
            if (kPalette[i].getRProperty() == c.getRProperty() &&
                kPalette[i].getGProperty() == c.getGProperty() &&
                kPalette[i].getBProperty() == c.getBProperty())
                return *solids_[i];
        return *solids_[0];
    }

    void DrawQuad(const Color& colour, int x, int y, int w, int h)
    {
        spriteBatch_->Draw(SolidFor(colour), Rectangle(x, y, w, h), Rectangle(0, 0, 1, 1),
                           Color::White);
    }

    void BeginQuads()
    {
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
    }

    /// Paints the whole currently-bound target with the asymmetric five-region pattern.
    void DrawPattern(int variant)
    {
        BeginQuads();
        DrawQuad(Slot(0, variant), 0, 0, kRT, kRT);
        DrawQuad(Slot(1, variant), 0, 0, kCorner, kCorner);
        DrawQuad(Slot(2, variant), kRT - kCorner, 0, kCorner, kCorner);
        DrawQuad(Slot(3, variant), 0, kRT - kCorner, kCorner, kCorner);
        DrawQuad(Slot(4, variant), kRT - kCorner, kRT - kCorner, kCorner, kCorner);
        DrawQuad(Slot(5, variant), kCorner, kCorner, kRT - 2 * kCorner, kRT - 2 * kCorner);
        spriteBatch_->End();
    }

    /// Paints one kMark x kMark marker at (@p x, @p y) and nothing else.
    void DrawMark(int x, int y, const Color& colour)
    {
        BeginQuads();
        DrawQuad(colour, x, y, kMark, kMark);
        spriteBatch_->End();
    }

    // ---------------------------------------------------------------------
    // Expectations
    // ---------------------------------------------------------------------

    static std::vector<Color> Expected(int variant)
    {
        std::vector<Color> e;
        e.reserve(static_cast<std::size_t>(kRT) * kRT);
        for (int y = 0; y < kRT; ++y)
            for (int x = 0; x < kRT; ++x) e.push_back(PatternTexel(x, y, variant));
        return e;
    }

    static std::vector<Color> Uniform(const Color& c)
    {
        return std::vector<Color>(static_cast<std::size_t>(kRT) * kRT, c);
    }

    static void Stamp(std::vector<Color>& e, int x, int y, const Color& colour)
    {
        for (int dy = 0; dy < kMark; ++dy)
            for (int dx = 0; dx < kMark; ++dx)
                e[static_cast<std::size_t>(y + dy) * kRT + (x + dx)] = colour;
    }

    /// The producer pattern of round @p variant with one marker stamped over it.
    static std::vector<Color> ExpectedWithMark(int variant, int x, int y, const Color& colour)
    {
        std::vector<Color> e = Expected(variant);
        Stamp(e, x, y, colour);
        return e;
    }

    /// A uniformly @p base surface with one marker stamped over it.
    static std::vector<Color> ExpectedBaseWithMark(const Color& base, int x, int y,
                                                  const Color& colour)
    {
        std::vector<Color> e = Uniform(base);
        Stamp(e, x, y, colour);
        return e;
    }

    // ---------------------------------------------------------------------
    // Probing
    // ---------------------------------------------------------------------

    Probe ProbeSurface(const std::vector<Color>& expected,
                       const std::function<void(std::vector<Color>&)>& read)
    {
        Probe p;
        p.dest.assign(static_cast<std::size_t>(kRT) * kRT, Sentinel());
        p.window = p.dest.size();
        try
        {
            read(p.dest);
        }
        catch (const System::NotSupportedException&) { p.threwNotSupported = true; }
        catch (const std::exception& e) { p.threwSomethingElse = true; p.otherWhat = e.what(); }
        catch (...) { p.threwSomethingElse = true; p.otherWhat = "unknown"; }

        for (std::size_t i = 0; i < p.dest.size(); ++i)
        {
            if (Same(p.dest[i], Sentinel())) ++p.sentinelSurvivors;
            if (Same(p.dest[i], expected[i])) ++p.exact;
            else if (p.firstMismatch.empty())
                p.firstMismatch = " first@(" + std::to_string(i % kRT) + "," +
                                  std::to_string(i / kRT) + ") got " + ColorText(p.dest[i]) +
                                  " want " + ColorText(expected[i]);
        }
        return p;
    }

    Probe ProbeTarget(const RenderTarget2D& rt, const std::vector<Color>& expected)
    {
        return ProbeSurface(expected, [&rt](std::vector<Color>& dest) {
            rt.GetData(0, nullptr, dest.data(), 0, static_cast<int>(dest.size()));
        });
    }

    Probe ProbeFace(const RenderTargetCube& cube, int face, const std::vector<Color>& expected)
    {
        return ProbeSurface(expected, [&cube, face](std::vector<Color>& dest) {
            cube.GetData(static_cast<CubeMapFace>(face), 0, nullptr, dest.data(), 0,
                         static_cast<int>(dest.size()));
        });
    }

    /// Judges one probe against this renderer's declared readback contract.
    void Judge(const Probe& p, Support required, const std::string& label)
    {
        const std::string facts =
            " [threwNotSupported=" + std::string(p.threwNotSupported ? "1" : "0") +
            " threwOther=" + (p.threwSomethingElse ? ("1:" + p.otherWhat) : "0") +
            " exact=" + std::to_string(p.exact) + "/" + std::to_string(p.window) +
            " sentinelSurvivors=" + std::to_string(p.sentinelSurvivors) + p.firstMismatch + "]";

        if (required == Support::Exact)
            check(!p.threwNotSupported && !p.threwSomethingElse && p.exact == p.window,
                  label + " -- exact content required" + facts);
        else
            check(p.threwNotSupported && !p.threwSomethingElse && p.sentinelSurvivors == p.window,
                  label + " -- deterministic NotSupportedException with the destination untouched "
                          "required" + facts);
    }

    // ---------------------------------------------------------------------
    // Bind-cycle helpers -- every one of these is a COMPLETE public cycle and
    // NOTHING between two of them ever forces a flush.
    // ---------------------------------------------------------------------

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, RenderTargetUsage usage,
                                               int multiSampleCount = 0)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                DepthFormat::None, multiSampleCount, usage);
    }

    void CyclePattern(GraphicsDevice& dev, RenderTarget2D& rt, int variant)
    {
        dev.SetRenderTarget(&rt);
        DrawPattern(variant);
        dev.SetRenderTarget(nullptr);
    }

    void CycleMark(GraphicsDevice& dev, RenderTarget2D& rt, int x, int y, const Color& colour)
    {
        dev.SetRenderTarget(&rt);
        DrawMark(x, y, colour);
        dev.SetRenderTarget(nullptr);
    }

    void CycleFacePattern(GraphicsDevice& dev, RenderTargetCube& cube, int face, int variant)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        DrawPattern(variant);
        dev.SetRenderTargets({});
    }

    void CycleFaceMark(GraphicsDevice& dev, RenderTargetCube& cube, int face, int x, int y,
                       const Color& colour)
    {
        dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
        DrawMark(x, y, colour);
        dev.SetRenderTargets({});
    }

    // =====================================================================
    // S -- segmentation core, RenderTarget2D
    // =====================================================================

    /**
     * @brief S1 -- the decisive shape. Two DiscardContents cycles of ONE target, no barrier.
     *
     * The second bind must discard the first cycle's whole pattern. A renderer that collapses the
     * two cycles into one pass clears once and then runs both draws, returning the pattern with
     * the marker on top.
     */
    void RunSameTargetDiscard(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *rt, 0);
        CycleMark(dev, *rt, 0, 0, kPalette[2]);
        if (kContract.segmentsBindCycles)
            Judge(ProbeTarget(*rt, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
                  kContract.readback,
                  "S1 same-target discard: the second bind cycle of a DiscardContents target "
                  "discards the first cycle's content");
        else
            Judge(ProbeTarget(*rt, ExpectedWithMark(0, 0, 0, kPalette[2])), kContract.readback,
                  "S1 same-target discard: this renderer COLLAPSES the two bind cycles into one "
                  "pass, so the first cycle's content survives (declared open defect)");
    }

    /// S2 -- the same shape on a PreserveContents target: both cycles' content must survive.
    void RunSameTargetPreserve(GraphicsDevice& dev)
    {
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        CyclePattern(dev, *rt, 0);
        CycleMark(dev, *rt, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*rt, ExpectedWithMark(0, 0, 0, kPalette[2])), kContract.readback,
              "S2 same-target preserve: two bind cycles of a PreserveContents target compose");
    }

    /**
     * @brief S3 -- an explicit Clear() in the SECOND cycle of a DiscardContents target.
     *
     * The clear belongs to the second logical pass only, so the first cycle's pattern must be gone
     * and the whole target must read as the clear colour except for the marker.
     */
    void RunClearInSecondCycle(GraphicsDevice& dev)
    {
        if (!RequireSegments("S3 clear in second cycle")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *rt, 0);
        dev.SetRenderTarget(rt.get());
        dev.Clear(kPalette[4]);
        DrawMark(kRT - kMark, kRT - kMark, kPalette[1]);
        dev.SetRenderTarget(nullptr);
        Judge(ProbeTarget(*rt, ExpectedBaseWithMark(kPalette[4], kRT - kMark, kRT - kMark,
                                                    kPalette[1])),
              kContract.readback,
              "S3 clear in second cycle: an explicit Clear() applies to the cycle that issued it");
    }

    /// S4 -- target A -> target B -> target A, all DiscardContents.
    void RunTargetRoundTrip(GraphicsDevice& dev)
    {
        if (!RequireSegments("S4 A->B->A")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CyclePattern(dev, *b, 1);
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "S4 A->B->A: A's third cycle discards A's own first cycle");
        Judge(ProbeTarget(*b, Expected(1)), kContract.readback,
              "S4 A->B->A: B keeps its own content and never receives A's");
    }

    /**
     * @brief S5 -- two independent targets, one cycle each, identical size and format.
     *
     * Nothing is rebound here at all, so this isolates a second failure mode from S4: whether each
     * pass's vertex/index data survives the recording of the OTHER pass. Both targets are painted
     * with different producer rounds, so a shared arena cursor that restarts per pass makes one
     * target render the other's geometry.
     */
    void RunTwoIndependentTargets(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CyclePattern(dev, *b, 1);
        Judge(ProbeTarget(*a, Expected(0)), kContract.readback,
              "S5 two targets: the first target keeps its own geometry");
        Judge(ProbeTarget(*b, Expected(1)), kContract.readback,
              "S5 two targets: the second target keeps its own geometry");
    }

    /// S6 -- target -> backbuffer -> target.
    void RunTargetBackbufferTarget(GraphicsDevice& dev)
    {
        if (!RequireSegments("S6 target->backbuffer->target")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        BeginQuads();
        DrawQuad(kPalette[3], 0, 0, kBBW, kBBH);
        spriteBatch_->End();
        CycleMark(dev, *a, kRT - kMark, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, 0, kPalette[2])),
              kContract.readback,
              "S6 target->backbuffer->target: an intervening backbuffer cycle does not merge the "
              "target's two cycles");
    }

    /// S7 -- Preserve A -> Discard B -> Preserve A: each target keeps its own load action.
    void RunPreserveDiscardPreserve(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CyclePattern(dev, *b, 1);
        CycleMark(dev, *a, kCorner, kCorner, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedWithMark(0, kCorner, kCorner, kPalette[2])),
              kContract.readback,
              "S7 preserve/discard/preserve: A's preservation is not replaced by B's discard");
        Judge(ProbeTarget(*b, Expected(1)), kContract.readback,
              "S7 preserve/discard/preserve: B's discard is not replaced by A's preservation");
    }

    /// S8 -- three DiscardContents cycles: only the last one's content survives.
    void RunThreeDiscardCycles(GraphicsDevice& dev)
    {
        if (!RequireSegments("S8 three discard cycles")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CycleMark(dev, *a, 0, 0, kPalette[1]);
        CycleMark(dev, *a, kRT - kMark, kRT - kMark, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, kRT - kMark,
                                                   kPalette[2])),
              kContract.readback,
              "S8 three discard cycles: only the final cycle's content survives");
    }

    /**
     * @brief S9 -- three PreserveContents cycles writing the SAME texels in a known order.
     *
     * Every marker lands on the same 2x2 corner in a different colour, so the final value proves
     * the cycles executed in public order and not, say, sorted or reversed.
     */
    void RunThreePreserveCyclesOrdering(GraphicsDevice& dev)
    {
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        CyclePattern(dev, *a, 0);
        CycleMark(dev, *a, 0, 0, kPalette[1]);
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedWithMark(0, 0, 0, kPalette[2])), kContract.readback,
              "S9 ordering: overlapping markers from three cycles resolve in public order");
    }

    /// S10 -- two cycles of one DiscardContents target under DIFFERENT viewports.
    void RunViewportPerCycle(GraphicsDevice& dev)
    {
        if (!RequireSegments("S10 viewport per cycle")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);

        dev.SetRenderTarget(a.get());
        dev.setViewportProperty(Viewport(0, 0, kMark, kMark));
        DrawQuad2DInViewport(kPalette[2]);
        dev.SetRenderTarget(nullptr);

        if (kContract.spriteViewportIsLocal)
            Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
                  kContract.readback,
                  "S10 viewport per cycle: the second cycle's sub-viewport applies to that cycle "
                  "only");
        else
            Judge(ProbeTarget(*a, Uniform(kPalette[2])), kContract.readback,
                  "S10 viewport per cycle: this renderer's SpriteBatch ignores a sub-Viewport, so "
                  "the second cycle covers the whole target -- what is asserted here is that the "
                  "first cycle's pattern is still gone");
    }

    /// Fills the whole ACTIVE viewport (viewport-local coordinates, FNA SpriteBatch semantics).
    void DrawQuad2DInViewport(const Color& colour)
    {
        BeginQuads();
        DrawQuad(colour, 0, 0, kRT, kRT);
        spriteBatch_->End();
    }

    /// S11 -- two cycles of one DiscardContents target under DIFFERENT scissor rectangles.
    void RunScissorPerCycle(GraphicsDevice& dev)
    {
        if (!RequireSegments("S11 scissor per cycle")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);

        dev.SetRenderTarget(a.get());
        RasterizerState scissored = RasterizerState::CullNone;
        scissored.setScissorTestEnableProperty(true);
        dev.setScissorRectangleProperty(Rectangle(0, 0, kMark, kMark));
        {
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth,
                                &scissored);
            DrawQuad(kPalette[2], 0, 0, kRT, kRT);
            spriteBatch_->End();
        }
        dev.SetRenderTarget(nullptr);

        if (kContract.spriteScissorApplies)
            Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
                  kContract.readback,
                  "S11 scissor per cycle: the second cycle's scissor applies to that cycle only");
        else
            Judge(ProbeTarget(*a, Uniform(kPalette[2])), kContract.readback,
                  "S11 scissor per cycle: this renderer's SpriteBatch ignores ScissorRectangle, so "
                  "the second cycle covers the whole target -- what is asserted here is that the "
                  "first cycle's pattern is still gone");
    }

    /// S12 -- A -> B -> A -> B: four cycles across two targets, alternating.
    void RunAlternatingTargets(GraphicsDevice& dev)
    {
        if (!RequireSegments("S12 A->B->A->B")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CyclePattern(dev, *b, 1);
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        CycleMark(dev, *b, kRT - kMark, kRT - kMark, kPalette[5]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback, "S12 A->B->A->B: A ends at its own final cycle");
        Judge(ProbeTarget(*b, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, kRT - kMark,
                                                   kPalette[5])),
              kContract.readback, "S12 A->B->A->B: B ends at its own final cycle");
    }

    // =====================================================================
    // C -- RenderTargetCube faces
    // =====================================================================

    /// C1 -- face 0 -> face 1 -> face 0 on a DiscardContents cube.
    void RunCubeFaceRoundTrip(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargets)
        {
            skip("C1 cube A->B->A: skipped -- no RenderTargetCube on this renderer");
            return;
        }
        if (!RequireSegments("C1 cube A->B->A")) return;
        RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        CycleFacePattern(dev, cube, 0, 0);
        CycleFacePattern(dev, cube, 1, 1);
        CycleFaceMark(dev, cube, 0, 0, 0, kPalette[2]);
        Judge(ProbeFace(cube, 0, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.cubeReadback,
              "C1 cube A->B->A: face 0's third cycle discards face 0's own first cycle");
        Judge(ProbeFace(cube, 1, Expected(1)), kContract.cubeReadback,
              "C1 cube A->B->A: face 1 keeps its own content");
    }

    /// C2 -- the same face sequence on a PreserveContents cube.
    void RunCubeFacePreserve(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargets)
        {
            skip("C2 cube preserve A->B->A: skipped -- no RenderTargetCube on this renderer");
            return;
        }
        RenderTargetCube cube(dev, kRT, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        CycleFacePattern(dev, cube, 2, 0);
        CycleFacePattern(dev, cube, 3, 1);
        CycleFaceMark(dev, cube, 2, kCorner, kCorner, kPalette[2]);
        Judge(ProbeFace(cube, 2, ExpectedWithMark(0, kCorner, kCorner, kPalette[2])),
              kContract.cubeReadback,
              "C2 cube preserve A->B->A: face 2 preserves across an intervening face");
        Judge(ProbeFace(cube, 3, Expected(1)), kContract.cubeReadback,
              "C2 cube preserve A->B->A: face 3 keeps its own content");
    }

    // =====================================================================
    // M -- multiple render targets
    // =====================================================================

    /**
     * @brief Binds two colour attachments, or reports that this renderer refuses to.
     *
     * `SupportsCapability(MultipleRenderTargets)` cannot be trusted as the gate: it has no override
     * on any renderer and `IGraphicsRenderer`'s default answers true, so Software (whose
     * `SetRenderTargets` throws outright) claims MRT support. The only honest probe is the bind
     * itself, with the backbuffer restored if it fails.
     */
    bool TryBindMrt(GraphicsDevice& dev, RenderTarget2D& first, RenderTarget2D& second)
    {
        try
        {
            dev.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&first)),
                                  RenderTargetBinding(static_cast<Texture*>(&second))});
            return true;
        }
        catch (...)
        {
            try { dev.SetRenderTargets({}); } catch (...) {}
            return false;
        }
    }

    /// M1 -- two MRT bind cycles of the same attachment set.
    void RunMrtCycles(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("M1 MRT cycles: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("M1 MRT cycles")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        if (!TryBindMrt(dev, *a, *b))
        {
            skip("M1 MRT cycles: skipped -- this renderer refuses two simultaneous render targets");
            return;
        }
        DrawPattern(0);
        dev.SetRenderTargets({});

        (void) TryBindMrt(dev, *a, *b);
        DrawMark(0, 0, kPalette[2]);
        dev.SetRenderTargets({});

        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "M1 MRT cycles: attachment 0's second MRT bind cycle discards the first");
    }

    /// M2 -- the attachment SET changes between cycles: {a,b} then {a,c}.
    void RunMrtAttachmentSetChange(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("M2 MRT attachment set: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("M2 MRT attachment set")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto c = MakeTarget(dev, RenderTargetUsage::DiscardContents);

        if (!TryBindMrt(dev, *a, *b))
        {
            skip("M2 MRT attachment set: skipped -- this renderer refuses two simultaneous render "
                 "targets");
            return;
        }
        DrawPattern(0);
        dev.SetRenderTargets({});

        (void) TryBindMrt(dev, *a, *c);
        DrawMark(kRT - kMark, 0, kPalette[5]);
        dev.SetRenderTargets({});

        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, 0, kPalette[5])),
              kContract.readback,
              "M2 MRT attachment set: changing only the second attachment still starts a new "
              "logical pass for the first");
        // The 2D sprite pipeline writes colour attachment 0 only, so b holds exactly what ITS own
        // bind cycle's load action left -- a single uniform colour, whose exact value is the
        // renderer's own choice of what an unwritten extra attachment ends up as and is deliberately
        // not asserted here. What IS asserted is that the SECOND cycle, which does not include b,
        // never reaches into it: a marker or a pattern texel showing up would mean the attachment
        // set leaked across the boundary.
        {
            Probe pb = ProbeTarget(*b, Uniform(DiscardColour()));
            bool uniform = !pb.dest.empty();
            bool clean   = true;
            for (const Color& c : pb.dest)
            {
                if (!Same(c, pb.dest[0])) uniform = false;
                if (Same(c, kPalette[5])) clean = false;
            }
            check(!pb.threwNotSupported && !pb.threwSomethingElse && uniform && clean,
                  "M2 MRT attachment set: the attachment dropped from the set is untouched by the "
                  "later cycle [uniform=" + std::string(uniform ? "1" : "0") + " marker-free=" +
                  (clean ? "1" : "0") + " colour=" +
                  (pb.dest.empty() ? std::string("none") : ColorText(pb.dest[0])) + "]");
        }
    }

    // =====================================================================
    // K -- multisample targets
    // =====================================================================

    /**
     * @brief K1 -- two bind cycles of a MULTISAMPLED DiscardContents target.
     *
     * The resolve of each segment must complete before the next segment loads, and the final
     * readback must see the resolved single-sample content of the LAST cycle only. A renderer where
     * `MultiSampleCount` comes back 0 never engaged MSAA (Vulkan only multisamples a render target
     * when the BACKBUFFER was created multisampled) -- that is reported, and the single-sample
     * segmentation assertion still runs.
     */
    void RunMsaaCycles(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact || !kContract.msaaTargetReadback)
        {
            skip("K1 MSAA cycles: skipped -- this renderer has no honest readback of a multisampled "
                 "render target");
            return;
        }
        if (!RequireSegments("K1 MSAA cycles")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents, kMsaaRequest);
        const int applied = a->getMultiSampleCountProperty();
        if (kContract.preferMultiSampling)
            check(applied > 1,
                  "K1 MSAA cycles: this renderer declares that a multisampled backbuffer engages "
                  "render-target MSAA, so the target must report a real applied sample count "
                  "[applied=" + std::to_string(applied) + "]");
        CyclePattern(dev, *a, 0);
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "K1 MSAA cycles: two bind cycles of a multiSampleCount=" +
                  std::to_string(kMsaaRequest) + " target (applied=" + std::to_string(applied) +
                  ") stay separate");

        // K2: the same target interleaved with a second multisampled one, so each segment's
        // resolve must complete before the next segment of the same resource loads.
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents, kMsaaRequest);
        CyclePattern(dev, *a, 1);
        CyclePattern(dev, *b, 0);
        CycleMark(dev, *a, kRT - kMark, kRT - kMark, kPalette[5]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, kRT - kMark,
                                                   kPalette[5])),
              kContract.readback,
              "K2 MSAA A->B->A: the multisampled target ends at its own final cycle");
        Judge(ProbeTarget(*b, Expected(0)), kContract.readback,
              "K2 MSAA A->B->A: the interleaved multisampled target keeps its own content");
    }

    // =====================================================================
    // X -- REMED-GFX-129 classification (explicit Clear, not pass boundaries)
    // =====================================================================

    /// X1 -- a Clear() inside the ONLY bind cycle of a PreserveContents target.
    void RunClearOnPreserveSingleCycle(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("X1 GFX-129 single cycle: skipped -- no readback on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(kPalette[4]);
        DrawMark(0, 0, kPalette[1]);
        dev.SetRenderTarget(nullptr);

        Probe p = ProbeTarget(*rt, ExpectedBaseWithMark(kPalette[4], 0, 0, kPalette[1]));
        if (kContract.clearOnPreserveTarget)
        {
            Judge(p, Support::Exact,
                  "X1 GFX-129 single cycle: Clear() on a PreserveContents target is honoured");
        }
        else
        {
            // The declared, still-open defect: the marker lands, the clear does not. Asserting the
            // marker AND the absence of the clear colour keeps this falsifiable in both
            // directions -- it fails the day the clear starts working, and it fails the day the
            // marker stops working.
            const Color got = p.dest.empty() ? Sentinel() : p.dest[static_cast<std::size_t>(kRT)
                                                                   * (kRT - 1)];
            check(!p.threwNotSupported && !p.threwSomethingElse &&
                      Same(p.dest[0], kPalette[1]) && !Same(got, kPalette[4]),
                  "X1 GFX-129 single cycle: Clear() on a PreserveContents target is DROPPED here "
                  "(recorded open defect) -- marker=" +
                      (p.dest.empty() ? std::string("none") : ColorText(p.dest[0])) +
                      " background=" + ColorText(got) + " clearColour=" + ColorText(kPalette[4]));
        }
    }

    /// X2 -- a Clear() inside the SECOND bind cycle of a PreserveContents target.
    void RunClearOnPreserveSecondCycle(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("X2 GFX-129 second cycle: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("X2 GFX-129 second cycle")) return;
        auto rt = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        CyclePattern(dev, *rt, 0);
        dev.SetRenderTarget(rt.get());
        dev.Clear(kPalette[4]);
        DrawMark(0, 0, kPalette[1]);
        dev.SetRenderTarget(nullptr);

        Probe p = ProbeTarget(*rt, ExpectedBaseWithMark(kPalette[4], 0, 0, kPalette[1]));
        if (kContract.clearOnPreserveTarget)
        {
            Judge(p, Support::Exact,
                  "X2 GFX-129 second cycle: Clear() in a later cycle of a PreserveContents target "
                  "is honoured");
        }
        else
        {
            // GFX-140's segmentation cannot repair this: the second cycle IS its own pass, and
            // that pass still has no way to deliver a clear colour through LOAD_OP_LOAD. What must
            // hold is that the marker landed and the first cycle's pattern is still underneath.
            const std::vector<Color> preserved = ExpectedWithMark(0, 0, 0, kPalette[1]);
            std::size_t exactPreserved = 0;
            for (std::size_t i = 0; i < p.dest.size(); ++i)
                if (Same(p.dest[i], preserved[i])) ++exactPreserved;
            check(!p.threwNotSupported && !p.threwSomethingElse &&
                      exactPreserved == p.dest.size(),
                  "X2 GFX-129 second cycle: Clear() in a later cycle is DROPPED here (recorded "
                  "open defect) and the earlier cycle's content shows through -- exactPreserved=" +
                      std::to_string(exactPreserved) + "/" + std::to_string(p.dest.size()));
        }
    }

    /// X3 -- draw, then Clear(), then draw, all inside ONE bind cycle.
    void RunClearAfterDraw(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("X3 clear after draw: skipped -- no readback on this renderer");
            return;
        }
        auto rt = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(rt.get());
        DrawPattern(0);
        dev.Clear(kPalette[4]);
        DrawMark(0, 0, kPalette[1]);
        dev.SetRenderTarget(nullptr);

        Probe p = ProbeTarget(*rt, ExpectedBaseWithMark(kPalette[4], 0, 0, kPalette[1]));
        if (kContract.clearAfterDrawWins)
        {
            Judge(p, Support::Exact,
                  "X3 clear after draw: a Clear() issued after a draw wipes that draw");
        }
        else
        {
            const std::vector<Color> loadOpOnly = ExpectedWithMark(0, 0, 0, kPalette[1]);
            std::size_t exactLoadOpOnly = 0;
            for (std::size_t i = 0; i < p.dest.size(); ++i)
                if (Same(p.dest[i], loadOpOnly[i])) ++exactLoadOpOnly;
            check(!p.threwNotSupported && !p.threwSomethingElse &&
                      exactLoadOpOnly == p.dest.size(),
                  "X3 clear after draw: this renderer's only clear mechanism is the pass load "
                  "action, so the earlier draw survives (recorded, same root cause as GFX-129) -- "
                  "exactLoadOpOnly=" + std::to_string(exactLoadOpOnly) + "/" +
                      std::to_string(p.dest.size()));
        }
    }

    // =====================================================================
    // R -- repetition: caches and resources must stay bounded
    // =====================================================================

    /**
     * @brief R1 -- the A->B->A shape repeated, to show a per-logical-pass permanent object is not
     *        being created.
     *
     * The assertion is behavioural, not a memory measurement: after many segments the same
     * sequence must still produce the same exact pixels. A renderer that keyed a cache on a segment
     * identity would either grow without bound or start missing.
     */
    void RunRepeatedSegments(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("R1 repetition: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("R1 repetition")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        for (int i = 0; i < 8; ++i)
        {
            CyclePattern(dev, *a, i % 2);
            CyclePattern(dev, *b, (i + 1) % 2);
        }
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "R1 repetition: 17 logical segments in one flush window still resolve exactly");
        Judge(ProbeTarget(*b, Expected(0)), kContract.readback,
              "R1 repetition: the interleaved target still ends at its own final cycle");
    }

    // =====================================================================
    // P -- the Present path (RecordCommandBuffer's full mode, not the
    //      single-target readback flush every other check triggers)
    // =====================================================================

    /**
     * @brief Queues P1/P2/P3's work and then lets the frame END.
     *
     * Every other check in this file reaches the recorder through a readback, which on a deferred
     * renderer may flush ONE target at a time and therefore hide anything that only goes wrong when
     * several targets' passes are recorded into the SAME command buffer. These three targets are
     * left entirely un-read until the next frame, so their passes are recorded by the ordinary
     * Present path, in one command buffer, in one submission.
     */
    void QueuePresentPathWork(GraphicsDevice& dev)
    {
        pa_ = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        pb_ = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        pc_ = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *pa_, 0);
        CyclePattern(dev, *pb_, 1);
        CyclePattern(dev, *pc_, 0);
        CycleMark(dev, *pc_, 0, 0, kPalette[2]);
        // P7: the producer pattern draws the SAME rectangles in every round -- only the source
        // texture differs -- so pa_/pb_/pc_ cannot see a shared vertex arena whose cursor restarts
        // per pass being overwritten between them. These two differ in GEOMETRY: if one pass's
        // vertices were replaced by a later pass's before the queue was submitted, the marker
        // would land in the wrong corner. P6 makes the same measurement, but only on a renderer
        // that can read its backbuffer -- SdlGpu cannot, so that route skips there and this one,
        // riding the ordinary Present path, is the only place the question gets asked
        // (REMED-GFX-145's own false-positive audit).
        pd_ = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        pe_ = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CycleMark(dev, *pd_, 0, 0, kPalette[1]);
        CycleMark(dev, *pe_, kRT - kMark, kRT - kMark, kPalette[5]);
    }

    /// P1/P2/P3/P7 -- read what the Present path actually produced.
    void RunPresentPathChecks()
    {
        if (kContract.readback != Support::Exact)
        {
            skip("P1 present path: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("P1 present path")) return;
        Judge(ProbeTarget(*pa_, Expected(0)), kContract.readback,
              "P1 present path: the first of three targets recorded into ONE command buffer keeps "
              "its own geometry");
        Judge(ProbeTarget(*pb_, Expected(1)), kContract.readback,
              "P2 present path: the second target keeps its own geometry");
        Judge(ProbeTarget(*pc_, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "P3 present path: a target bound twice before Present still gets two separate "
              "cycles");
        Judge(ProbeTarget(*pd_, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[1])),
              kContract.readback,
              "P7 present-path vertex arena: the first target's own GEOMETRY survives the "
              "recording of the second target's pass, with no backbuffer readback involved");
        Judge(ProbeTarget(*pe_, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, kRT - kMark,
                                                     kPalette[5])),
              kContract.readback,
              "P7 present-path vertex arena: the second target's own geometry is exact");
    }

    /**
     * @brief P4/P5 -- several targets recorded into ONE command buffer, forced from the BACKBUFFER.
     *
     * Reading a render target may flush only that target, so P1..P3 cannot prove what happens when
     * two different targets' passes are recorded side by side. Reading the BACKBUFFER forces the
     * full record of every pending target in one command buffer without naming any of them, and
     * without being a per-target barrier. If the passes shared a per-frame vertex arena whose
     * cursor restarted per pass, the earlier pass would draw the later pass's geometry.
     */
    void RunSharedCommandBufferRecord(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("P4 shared command buffer: skipped -- no readback on this renderer");
            return;
        }
        if (!RequireSegments("P4 shared command buffer")) return;
        auto a = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto c = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        CyclePattern(dev, *a, 0);
        CyclePattern(dev, *b, 1);
        CyclePattern(dev, *c, 0);
        CycleMark(dev, *c, 0, 0, kPalette[2]);

        Color bb(0, 0, 0, 0);
        Rectangle one(0, 0, 1, 1);
        bool backbufferReadable = true;
        try { dev.GetBackBufferData(&one, &bb, 0, 1); }
        catch (...) { backbufferReadable = false; }
        if (!backbufferReadable)
        {
            skip("P4 shared command buffer: skipped -- this renderer cannot read the backbuffer, so "
                 "the full record cannot be forced without naming a target");
            return;
        }

        Judge(ProbeTarget(*a, Expected(0)), kContract.readback,
              "P4 shared command buffer: the first target keeps its own geometry when three "
              "targets are recorded together");
        Judge(ProbeTarget(*b, Expected(1)), kContract.readback,
              "P4 shared command buffer: the second target keeps its own geometry");
        Judge(ProbeTarget(*c, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[2])),
              kContract.readback,
              "P5 shared command buffer: a target bound twice keeps two cycles even when other "
              "targets are recorded between them");

        // P6: the producer pattern draws the SAME rectangles in every round -- only the source
        // texture differs -- so P4 cannot see a shared vertex arena being overwritten between
        // passes. These two targets differ in GEOMETRY, not just colour: if one pass's vertices
        // were replaced by a later pass's before the queue was submitted, the marker would land in
        // the wrong corner.
        auto d = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        auto e = MakeTarget(dev, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(d.get());
        DrawMark(0, 0, kPalette[1]);
        dev.SetRenderTarget(nullptr);
        dev.SetRenderTarget(e.get());
        DrawMark(kRT - kMark, kRT - kMark, kPalette[5]);
        dev.SetRenderTarget(nullptr);
        try { dev.GetBackBufferData(&one, &bb, 0, 1); } catch (...) {}

        Judge(ProbeTarget(*d, ExpectedBaseWithMark(DiscardColour(), 0, 0, kPalette[1])),
              kContract.readback,
              "P6 shared vertex arena: the first target's own GEOMETRY survives the recording of "
              "the second target's pass");
        Judge(ProbeTarget(*e, ExpectedBaseWithMark(DiscardColour(), kRT - kMark, kRT - kMark,
                                                   kPalette[5])),
              kContract.readback,
              "P6 shared vertex arena: the second target's own geometry is exact");
    }

    /// R2 -- the same battery a second time, after a real Present has intervened.
    void RunAcrossFrames(GraphicsDevice& dev)
    {
        if (kContract.readback != Support::Exact)
        {
            skip("R2 across frames: skipped -- no readback on this renderer");
            return;
        }
        auto a = MakeTarget(dev, RenderTargetUsage::PreserveContents);
        CyclePattern(dev, *a, 0);
        // The ONLY deliberate flush in this file: it belongs to R2's subject, which is what
        // happens when the two cycles genuinely land in different flush windows.
        Judge(ProbeTarget(*a, Expected(0)), kContract.readback,
              "R2 across frames: the first cycle alone is exact");
        CycleMark(dev, *a, 0, 0, kPalette[2]);
        Judge(ProbeTarget(*a, ExpectedWithMark(0, 0, 0, kPalette[2])), kContract.readback,
              "R2 across frames: a cycle in a later flush window composes with the earlier one");
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.preferMultiSampling)
            gdm_->setPreferMultiSamplingProperty(true);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        for (std::size_t i = 0; i < kPalette.size(); ++i)
        {
            const Color& c = kPalette[i];
            solids_[i] = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<std::uint8_t>{
                    static_cast<std::uint8_t>(c.getRProperty()),
                    static_cast<std::uint8_t>(c.getGProperty()),
                    static_cast<std::uint8_t>(c.getBProperty()),
                    255,
                }));
        }
    }

    void Draw(const GameTime&) override
    {
        // Frame 0 is a warm-up: bgfx copies every createTexture payload at bgfx::frame(), so
        // without it the 1x1 producer sources would still be unuploaded during the first Draw.
        if (phase_ == 0)
        {
            phase_ = 1;
            BeginQuads();
            DrawQuad(kPalette[0], 0, 0, kBBW, kBBH);
            spriteBatch_->End();
            return;
        }
        if (phase_ == 2)
        {
            // Frame 2: everything queued at the end of frame 1 has been through a real Present.
            phase_ = 3;
            RunPresentPathChecks();
            std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        std::printf("REMED-GFX-140 render-target pass boundaries -- renderer %s\n", kContract.name);

        if (!kContract.rt2dTargets)
        {
            skip("no RenderTarget2D on this renderer -- nothing to segment");
            phase_ = 3;
            std::printf("%d/%d checks passed on %s\n", passCount_, totalCount_, kContract.name);
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            Exit();
            return;
        }

        RunSameTargetDiscard(dev);
        RunSameTargetPreserve(dev);
        RunClearInSecondCycle(dev);
        RunTargetRoundTrip(dev);
        RunTwoIndependentTargets(dev);
        RunTargetBackbufferTarget(dev);
        RunPreserveDiscardPreserve(dev);
        RunThreeDiscardCycles(dev);
        RunThreePreserveCyclesOrdering(dev);
        RunViewportPerCycle(dev);
        RunScissorPerCycle(dev);
        RunAlternatingTargets(dev);
        RunCubeFaceRoundTrip(dev);
        RunCubeFacePreserve(dev);
        RunMrtCycles(dev);
        RunMrtAttachmentSetChange(dev);
        RunMsaaCycles(dev);
        RunClearOnPreserveSingleCycle(dev);
        RunClearOnPreserveSecondCycle(dev);
        RunClearAfterDraw(dev);
        RunRepeatedSegments(dev);
        RunSharedCommandBufferRecord(dev);
        RunAcrossFrames(dev);
        // Queued, deliberately unread: frame 2 reads them after Present has recorded them all into
        // ONE command buffer.
        QueuePresentPathWork(dev);
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    RenderTargetPassBoundaryTest test;
    test.Run();
    return test.Result();
}
