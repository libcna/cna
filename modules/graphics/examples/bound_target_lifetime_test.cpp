// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-168: destroying a RenderTarget2D that is STILL the bound render target must never make
// the next SetRenderTarget transition unsafe, and must leave the next target and the backbuffer
// exactly correct.
//
//     RenderTarget2D a(dev, ...);
//     dev.SetRenderTarget(&a);
//     dev.Clear(...); draw ...
//     }                                        // a's DESTRUCTOR runs while a is still bound
//     dev.SetRenderTarget(nullptr);            // <-- EASYGL faults HERE, pre-fix
//
// THE DEFECT this file reproduces is EasyGL-local and it is an OWNERSHIP defect, not a GL-state one.
// `EasyGLRenderer` remembers the bound destination as a raw `IRenderTargetRenderer*`
// (`currentRt2D_`, and `currentRtCube_` / `currentMrtTargets_` for the other two binding shapes) and
// the next transition calls a VIRTUAL method on it -- `currentRt2D_->UnbindAsRenderTarget()` -- to
// perform that target's pending finalization (the MSAA resolve blit and the mip regeneration).
// Nothing cleared that pointer when the object behind it died, and nothing in the public layer does
// it either: `RenderTarget2D::Dispose()` refuses to dispose a bound target, but the DESTRUCTOR is
// `= default` and never routes through Dispose, so a scoped render target that goes out of scope
// while bound releases its renderer silently.
//
// It is NOT the destructor that faults, and it is NOT teardown: the fault is inside the next
// `GraphicsDevice::SetRenderTarget()`, on the main thread, with the leg's own earlier checks already
// printed.
//
// WHAT IS LOAD-BEARING is only that the target was STILL BOUND when it died. Every ordering that
// unbinds first -- legs A1 and A2 -- is safe even pre-fix, which is exactly why no existing fixture
// caught this: they all transition away from a target before releasing it.
//
// PENDING FINALIZATION is the reason this cannot be fixed by nulling a pointer and forgetting the
// question. `UnbindAsRenderTarget()` resolves the multisample colour renderbuffer into `colorTex_`
// and regenerates `colorTex_`'s mip chain -- real work, and work a later consumer can observe. What
// makes DETACHING correct here rather than lossy is that both of those operations write into storage
// the dying object OWNS: `colorTex_` (and the cube's `cubeTex_`) is a member, destroyed by the same
// destructor, and the only public routes to that content -- `RenderTarget2D::GetData` and sampling
// the target as a `Texture2D` -- both need the live wrapper. So a target destroyed while bound has
// no reachable observer for its own finalization. Legs D1 and E1 assert the converse directly: when
// the target IS unbound first, the resolve and the mip chain must still be exact.
//
// MRT is the one binding shape where that reasoning does NOT extend to the whole set: the other
// slots are live objects whose finalization IS observable, so leg L1 requires the survivor's mip
// chain to be regenerated even though its neighbour died mid-binding.
//
// THE ORACLE is never "did not crash". Every leg that transitions away from a destroyed target then
// renders a known pattern into the next target and into the backbuffer and compares byte-exactly, so
// a renderer that survives the transition with corrupted framebuffer state fails just as loudly as
// one that faults.
//
// PROCESS ISOLATION: the pre-fix failure is a SIGSEGV, not an assertion, so one bad leg would take
// down the whole shard and report nothing about the legs behind it. Run with no arguments this
// binary is a SUPERVISOR: it re-execs itself once per leg as `--leg=<id>` and classifies each child
// as PASS / FAIL / CRASH-with-the-signal-named / TIMEOUT. Each leg prints a `[STEP]` line before
// every public operation, so the last line before a crash names the exact call that faulted.
//
// LEGS
//   A1 unbind-then-release      the safe ordering: transition away, sample, THEN release
//   A2 release after a switch   released after SetRenderTarget(B), also safe
//   B1 THE FINDING (backbuffer) destroyed while bound, then SetRenderTarget(nullptr)
//   B2 THE FINDING (next target) destroyed while bound, then SetRenderTarget(B)
//   C1 work shapes              no work / Clear only / SpriteBatch / stock 3D / with depth
//   D1 MSAA                     resolve-before-release must be exact; destroyed-while-bound safe
//   E1 mipmap                   mip finalization must be exact; destroyed-while-bound safe
//   F1 cube                     a RenderTargetCube destroyed while one of its faces is bound
//   G1 across a swap            the transition, then a real Present; the next frame must be exact
//   G2 one Present later        a target created last frame, re-bound and released while bound
//   H1 alternation              A -> B -> A, with A destroyed while bound at the end
//   I1 address reuse            a new target built straight onto the freed one's storage
//   J1 device teardown          the transition one instruction before the device tears down
//   K1 unwinding                an exception destroys the bound target before any unbind
//   L1 MRT                      one slot of a bound multi-target set destroyed; survivor must resolve
//   M1 repetition               120 create/bind/destroy-while-bound cycles, then exact content
//   N1 two targets              two live targets, reverse destruction order, one bound
//   P1 the frame-end contract   Present() refuses a bound target identically alive or destroyed
//
// WHY THERE IS NO "destroyed, then Present" LEG: there cannot be one. `GraphicsDevice::Present()`
// refuses to present while a render target is bound, and it decides that from a BOOL that no
// destructor touches -- so the refusal is identical for a live and a destroyed bound target, and the
// defect's whole window is bounded by the next SetRenderTarget. Leg P1 asserts exactly that rather
// than leaving it as an assumption; it is also why legs G1/G2/J1 place their frame boundary AFTER
// the transition instead of before it.
//
// Exit code 0 = all checks PASS, 1 = any FAIL or CRASH.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX168_CAN_FORK 1
#else
#define CNA_GFX168_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kPW = 8;    ///< producer pattern width  -- non-square on purpose
    constexpr int kPH = 4;    ///< producer pattern height
    /// Backbuffer extent. Each source texel must cover an ODD number of destination pixels, so the
    /// middle pixel of a block lands EXACTLY on that texel's centre; with an even block the centre
    /// falls between two pixels and a linear-filtering renderer returns a blend of two neighbours
    /// instead of the texel itself. 9 pixels per texel keeps every comparison byte-exact under point
    /// AND linear sampling, rather than needing a tolerance that would also admit a wrong resource.
    constexpr int kBBBlock = 9;
    constexpr int kBBW = kPW * kBBBlock;   ///< 72
    constexpr int kBBH = kPH * kBBBlock;   ///< 36

    /** @brief Whether this renderer rasterizes and can read anything back at all. */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
#else
    constexpr bool kRasterizes = true;
#endif

#if defined(CNA_RENDERER_HEADLESS)
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr const char* kRendererName = "DIRECTX12";
#else
#error "REMED-GFX-168: this renderer has no declared bound-target lifetime contract."
#endif

    /**
     * @brief Whether a `RenderTargetCube` face can be rendered into at all.
     *
     * SDL_RENDERER has no cube path and HEADLESS does not rasterize. Declared rather than asserted,
     * exactly as the neighbouring render-target fixtures already do.
     */
    constexpr bool kRenderTargetCubeSupported =
#if defined(CNA_RENDERER_SDL_RENDERER) || defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    /**
     * @brief Whether a render target's mip level > 0 is publicly readable after an unbind.
     *
     * Only the CONTROL halves of legs E1 and L1 need this -- the ones that prove the mip chain IS
     * produced when the target is unbound first, and therefore that skipping it for a target
     * destroyed while bound is a decision rather than an accident. The destroy-while-bound halves do
     * not depend on it and stay asserted everywhere.
     *
     * Declared false where a run MEASURED level 1 coming back all-zero rather than the flat colour
     * that was cleared into level 0: BGFX (`E1a the unbind regenerated level 1 (0/8) ... got
     * (0,0,0,0)`). WebGPU refuses `mipMap=true` outright with a NotSupportedException naming
     * plans/plan_webgpu.md WEBGPU-53/54, which the legs catch and declare at run time, so it does not need
     * to appear here. Silence is not a declaration, which is why the measured-zero case needs one.
     */
    constexpr bool kRenderTargetMipReadable =
#if defined(CNA_RENDERER_BGFX)
        false;
#else
        true;
#endif

    /**
     * @brief Whether the mip level > 0 of a MULTI-target set's member is publicly readable.
     *
     * Strictly narrower than kRenderTargetMipReadable: VULKAN regenerates a single render target's
     * mip chain (leg E1a passes there) but returns all-zero for level 1 of a target bound as part of
     * an MRT set (`L1 the surviving MRT slot's mip chain was still regenerated (0/8) ... got
     * (0,0,0,0)`). Recorded as an independent finding rather than folded into this task's subject;
     * L1's level-0 check and its backbuffer check stay asserted on every renderer. LLGL measures the
     * identical (0,0,0,0) result running on its own Vulkan module, unsurprising since that module
     * wraps real Vulkan and shares the same MRT/mip-regeneration path.
     */
    constexpr bool kMrtSlotMipReadable =
#if defined(CNA_RENDERER_BGFX) || defined(CNA_RENDERER_VULKAN)
        false;
#else
        true;
#endif

    /**
     * @brief Whether an MSAA render target's RESOLVED colour is publicly readable after an unbind.
     *
     * Leg D1's control half only. This was declared false on BGFX, where the resolved level 0
     * measured (0,0,0,0) against a flat (70,145,210,255) clear. That gap was REMED-GFX-154 and is
     * fixed, so the BGFX arm is gone and D1a measures the resolved colour like every other renderer.
     */
    constexpr bool kMsaaResolveReadable = true;

    /**
     * @brief The producer pattern texel at (@p x, @p y), (0, 0) being the TOP-LEFT corner.
     *
     * R is a function of x alone and G of y alone, so the pair (R, G) is unique across all 32 texels
     * and identifies both axes independently. B mixes the two, so a 180-degree rotation is
     * distinguishable, and A takes four distinct non-trivial values, so a dropped or forced-opaque
     * alpha cannot pass. Every channel is a multiple of 5 well away from 0 and 255, so a zero-filled
     * or poison image can never coincide with it.
     */
    Color PatternColor(int x, int y)
    {
        const int r = 20 + x * 30;                       // 20, 50, ... 230
        const int g = 25 + y * 70;                       // 25, 95, 165, 235
        const int b = 40 + ((x * 5 + y * 3) % 7) * 30;   // 40 .. 220, asymmetric in both axes
        const int a = 255 - ((x + 2 * y) % 4) * 55;      // 255, 200, 145, 90
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /**
     * @brief A second, unmistakably different pattern, for legs that hold two targets at once.
     *
     * Its R decreases with x where PatternColor's increases, so a leg that reads the wrong one of two
     * live targets fails on every texel rather than on a few.
     */
    Color AltPatternColor(int x, int y)
    {
        const int r = 245 - x * 30;                      // 245, 215, ... 35
        const int g = 235 - y * 70;                       // 235, 165, 95, 25
        const int b = 30 + ((x * 3 + y * 5) % 7) * 30;
        const int a = 90 + ((x + 3 * y) % 4) * 55;        // 90, 145, 200, 255
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /** @brief Formats a colour as R,G,B,A for diagnostics. */
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /**
     * @brief Whether @p e is a renderer REFUSING mipmapped render targets rather than a real failure.
     *
     * Matched on the message rather than the type on purpose: WebGPU raises a plain
     * `std::runtime_error` here (deliberately, so the XNA layer's promised mip chain cannot silently
     * be absent -- see WebGPURenderer::CreateRenderTarget2D), and narrowing to that type alone
     * would swallow every other runtime_error the leg could hit.
     */
    bool MipRefusal(const std::exception& e)
    {
        const std::string what = e.what();
        return what.find("mip") != std::string::npos &&
               (what.find("not implemented") != std::string::npos ||
                what.find("not supported") != std::string::npos);
    }

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// A flat colour for the legs whose oracle must survive mip reduction: a uniform image box-filters
    /// to itself, so level 1 is byte-comparable to level 0 without modelling the filter.
    const Color kFlat(70, 145, 210, 255);
    /// A second flat colour, for the surviving slot of a partially destroyed multi-target set.
    const Color kFlatAlt(215, 60, 125, 255);
}

/**
 * @brief REMED-GFX-168's public contract: a render target destroyed WHILE BOUND is safe, and the
 *        next target and the backbuffer stay exactly correct.
 */
class BoundTargetLifetimeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    bool boundaryDeclared_ = false;
    int  frame_ = 0;
    std::string onlyLeg_;

    Texture2D patternTex_;      ///< kPW x kPH, PatternColor -- what a producer must contain.
    Texture2D altPatternTex_;   ///< kPW x kPH, AltPatternColor.

    /// Leg G1/G2's target, held across a public frame boundary so its release can be placed on
    /// either side of the frame's own Present.
    std::unique_ptr<RenderTarget2D> acrossFrames_;
    /// Leg J1's target, deliberately left BOUND so device teardown happens with a live binding.
    std::unique_ptr<RenderTarget2D> heldBound_;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    /** @brief Records something this renderer genuinely cannot measure, and marks the run explained. */
    void boundary(const std::string& text)
    {
        boundaryDeclared_ = true;
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    /**
     * @brief Names the public operation about to be issued.
     *
     * The pre-fix outcome is a signal, so the LAST line a child printed is the only record of which
     * call faulted. Flushed immediately for exactly that reason.
     */
    void step(const std::string& text) const
    {
        std::printf("[STEP] %s\n", text.c_str());
        std::fflush(stdout);
    }

    /** @brief Whether this process should run leg @p id. */
    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
    }

    // ---------------------------------------------------------------- readback

    /** @brief The outcome of a public readback, poison-prefilled so "wrote nothing" is visible. */
    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;
        int w = 0;
        int h = 0;

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(y) * w + x];
        }
    };

    static Readback ReadWholeTarget(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.w = w; r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size())); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /** @brief Reads mip @p level of @p target, whose extent is @p w x @p h at that level. */
    static Readback ReadTargetLevel(RenderTarget2D& target, int level, int w, int h)
    {
        Readback r;
        r.w = w; r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        const Rectangle whole(0, 0, w, h);
        try { target.GetData(level, &whole, r.pixels.data(), 0, static_cast<int>(r.pixels.size())); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /**
     * @brief Reads the kBBW x kBBH backbuffer through the rectangle-LESS overload.
     *
     * REMED-GFX-165 is fixed: a rectangle-less GetBackBufferData now sizes its region from the
     * authoritative PresentationParameters backbuffer rather than the renderer's live viewport, so it
     * reads the whole backbuffer correctly on every renderer. This used to name an explicit rectangle
     * to avoid that defect; the workaround has been removed now that the overload it worked around is
     * correct.
     */
    Readback ReadBackbuffer(GraphicsDevice& dev)
    {
        Readback r;
        r.w = kBBW; r.h = kBBH;
        r.pixels.assign(static_cast<std::size_t>(r.w) * r.h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { dev.GetBackBufferData(r.pixels.data(), 0, static_cast<int>(r.pixels.size())); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when the caller may assert on @p r. A declared-unreadable oracle is recorded as a
    /// boundary rather than a failure, so a renderer without the oracle still runs its other legs.
    bool Readable(const Readback& r, const std::string& label)
    {
        if (!kRasterizes || !r.ok())
        {
            boundary(label + ": oracle unavailable on " + kRendererName + " (" +
                     (!kRasterizes ? "non-rasterizing"
                                   : (r.threwNotSupported ? "NotSupportedException" : r.otherWhat)) +
                     ") -- boundary recorded");
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- comparisons

    using PatternFn = Color (*)(int, int);

    /// Asserts an image reproduces @p want exactly, naming the failure SHAPE rather than only a
    /// count -- an all-poison result (nothing was written) reads differently from an all-zero one
    /// (the destination was cleared and never drawn into), and the two mean different things.
    void CheckExact(const Readback& r, const std::string& label, PatternFn want)
    {
        int good = 0, poison = 0, zero = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                // The middle pixel of the block this source texel occupies. Every destination size
                // this fixture uses gives an odd block, so this pixel's texture coordinate is
                // exactly the texel centre and the comparison is byte-exact whatever the renderer
                // filters with.
                const int px = (x * r.w) / kPW + (r.w / kPW) / 2;
                const int py = (y * r.h) / kPH + (r.h / kPH) / 2;
                const Color& got = r.at(px, py);
                if (Same(got, want(x, y))) { ++good; continue; }
                if (Same(got, Color(0xCD, 0xCD, 0xCD, 0xCD))) ++poison;
                if (got.getRProperty() == 0 && got.getGProperty() == 0 &&
                    got.getBProperty() == 0) ++zero;
                if (firstMismatch.empty())
                    firstMismatch = " first (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want " + ColorText(want(x, y)) + " got " + ColorText(got);
            }
        const int total = kPW * kPH;
        std::string shape;
        if (poison == total - good && poison > 0) shape = " [destination never written]";
        else if (zero == total - good && zero > 0) shape = " [destination cleared, never drawn]";
        check(good == total, label + " (" + std::to_string(good) + "/" + std::to_string(total) + ")" +
                             (good == total ? "" : shape + firstMismatch));
    }

    /** @brief Asserts every pixel of @p r equals @p want exactly. */
    void CheckFlat(const Readback& r, const std::string& label, const Color& want)
    {
        int good = 0;
        std::string firstMismatch;
        const int total = r.w * r.h;
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x)
            {
                const Color& got = r.at(x, y);
                if (Same(got, want)) { ++good; continue; }
                if (firstMismatch.empty())
                    firstMismatch = " first (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want " + ColorText(want) + " got " + ColorText(got);
            }
        check(good == total, label + " (" + std::to_string(good) + "/" + std::to_string(total) + ")" +
                             (good == total ? "" : firstMismatch));
    }

    // ---------------------------------------------------------------- producers

    static void FillQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    /** @brief Draws @p source across the whole current destination with point sampling. */
    void Blit(GraphicsDevice& dev, const Texture2D& source, int w, int h)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(source, Rectangle(0, 0, w, h),
                Rectangle(0, 0, source.getWidthProperty(), source.getHeightProperty()), Color::White);
        sb.End();
    }

    /** @brief Queues ONE stock-3D draw sampling @p src across the whole current destination. */
    void Consume3D(GraphicsDevice& dev, Texture2D* src)
    {
        VertexPositionTexture q[6];
        FillQuad(q);
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(src);
        fx.VertexColorEnabled = false;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    /** @brief Binds @p rt, fills it with @p source's pattern, and leaves it BOUND. */
    void FillBoundTarget(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        Blit(dev, source, rt.getWidthProperty(), rt.getHeightProperty());
    }

    /** @brief FillBoundTarget followed by the transition away from it. */
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        FillBoundTarget(dev, rt, source);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /** @brief Binds the BACKBUFFER as the destination and clears it to opaque black. */
    void BeginBackbuffer(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
    }

    // ---------------------------------------------------------------- shared oracles

    /**
     * @brief After a destroyed-while-bound target, the BACKBUFFER must still be exactly correct.
     *
     * The transition itself is the pre-fix fault site, so this both triggers the defect and measures
     * whether the state it left behind is usable. Uses the stock-3D route rather than SpriteBatch so
     * that a viewport or framebuffer left pointing at the dead target shows up as wrong pixels.
     */
    void RequireBackbufferExact(GraphicsDevice& dev, const std::string& label)
    {
        step(label + ": SetRenderTarget(nullptr) + Clear on the backbuffer");
        BeginBackbuffer(dev);
        step(label + ": stock-3D draw of the reference pattern onto the backbuffer");
        Consume3D(dev, &patternTex_);
        step(label + ": GetBackBufferData");
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, label))
            CheckExact(r, label + " the backbuffer after the transition is exact", PatternColor);
    }

    /** @brief After a destroyed-while-bound target, a NEXT render target must be exactly correct. */
    void RequireNextTargetExact(GraphicsDevice& dev, const std::string& label)
    {
        step(label + ": SetRenderTarget(B) -- the transition away from the destroyed target");
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, b, altPatternTex_);
        step(label + ": read B back");
        Readback rb = ReadWholeTarget(b, kPW, kPH);
        if (Readable(rb, label))
            CheckExact(rb, label + " the NEXT render target is exact", AltPatternColor);
        step(label + ": sample B onto the backbuffer");
        BeginBackbuffer(dev);
        Consume3D(dev, &b);
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, label))
            CheckExact(r, label + " the backbuffer sampling the NEXT target is exact", AltPatternColor);
    }

    // ---------------------------------------------------------------- legs

    /** @brief Leg A1 -- the SAFE ordering: transition away from the target, sample it, then release. */
    void LegA1(GraphicsDevice& dev)
    {
        step("A1: create A");
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            step("A1: produce into A and unbind");
            ProduceInto(dev, a, patternTex_);
            step("A1: read A back while it is still alive and already unbound");
            Readback ra = ReadWholeTarget(a, kPW, kPH);
            if (Readable(ra, "A1"))
                CheckExact(ra, "A1 an unbound, still-live target holds its own content", PatternColor);
            step("A1: sample A onto the backbuffer");
            BeginBackbuffer(dev);
            Consume3D(dev, &a);
            Readback r = ReadBackbuffer(dev);
            if (Readable(r, "A1"))
                CheckExact(r, "A1 the backbuffer sampling an unbound live target is exact",
                           PatternColor);
            step("A1: release A -- already unbound, the ordering every existing fixture uses");
        }
        RequireBackbufferExact(dev, "A1");
    }

    /** @brief Leg A2 -- released after SetRenderTarget(B), i.e. also never released while bound. */
    void LegA2(GraphicsDevice& dev)
    {
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        {
            step("A2: create A, produce into it, then switch the binding to B");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
            step("A2: SetRenderTarget(B) -- A is unbound by the switch, not by a destructor");
            FillBoundTarget(dev, b, altPatternTex_);
            step("A2: release A while B is bound");
        }
        step("A2: SetRenderTarget(nullptr)");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        step("A2: read B back");
        Readback rb = ReadWholeTarget(b, kPW, kPH);
        if (Readable(rb, "A2"))
            CheckExact(rb, "A2 B is exact after A was released while B was bound", AltPatternColor);
        RequireBackbufferExact(dev, "A2");
    }

    /**
     * @brief Leg B1 -- THE FINDING. The bound target dies, then SetRenderTarget(nullptr).
     *
     * The only difference from A1 is WHERE the closing brace sits: here it is before the transition
     * instead of after it.
     */
    void LegB1(GraphicsDevice& dev)
    {
        {
            step("B1: create A and bind it");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            step("B1: Clear + SpriteBatch draw into A, leaving A BOUND");
            FillBoundTarget(dev, a, patternTex_);
            step("B1: A's DESTRUCTOR runs here, while A is still the bound render target");
        }
        RequireBackbufferExact(dev, "B1");
    }

    /** @brief Leg B2 -- the same death, but the transition goes to another TARGET, not the backbuffer. */
    void LegB2(GraphicsDevice& dev)
    {
        {
            step("B2: create A and bind it");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            step("B2: Clear + SpriteBatch draw into A, leaving A BOUND");
            FillBoundTarget(dev, a, patternTex_);
            step("B2: A's DESTRUCTOR runs here, while A is still the bound render target");
        }
        RequireNextTargetExact(dev, "B2");
    }

    /**
     * @brief Leg C1 -- which WORK the dying target received, as five independent cases.
     *
     * Each case ends with the identical backbuffer oracle, so a case that fails names the one work
     * shape that is load-bearing rather than leaving "a draw happened" as the only description.
     */
    void LegC1(GraphicsDevice& dev)
    {
        // (a) bound and immediately released, with NO work at all. PreserveContents, because
        //     SetRenderTarget itself issues a Clear for a DiscardContents target -- with that usage
        //     "no work" is not expressible.
        {
            step("C1a: bind a PreserveContents target and release it with NO Clear and NO draw");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
        }
        RequireBackbufferExact(dev, "C1a");

        // (b) Clear only, no draw.
        {
            step("C1b: bind, Clear only, release while bound");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            dev.Clear(kFlat);
        }
        RequireBackbufferExact(dev, "C1b");

        // (c) SpriteBatch draws.
        {
            step("C1c: bind, SpriteBatch draw, release while bound");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            Blit(dev, patternTex_, kPW, kPH);
        }
        RequireBackbufferExact(dev, "C1c");

        // (d) stock 3D draws.
        {
            step("C1d: bind, stock-3D draw, release while bound");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            Consume3D(dev, &patternTex_);
        }
        RequireBackbufferExact(dev, "C1d");

        // (e) with a real depth-stencil attachment, and depth testing actually on.
        {
            step("C1e: bind a Depth24Stencil8 target, draw with depth on, release while bound");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                             0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            Consume3D(dev, &patternTex_);
        }
        RequireBackbufferExact(dev, "C1e");
    }

    /**
     * @brief Leg D1 -- MSAA. The resolve is real pending finalization; both orderings are measured.
     *
     * (a) is the control that gives the leg its meaning: with the target unbound FIRST, the resolve
     * must have happened and the content must be exact. (b) is the finding's shape at sample count
     * > 1, where the pending work is a blit rather than nothing.
     */
    void LegD1(GraphicsDevice& dev)
    {
        step("D1: probe the applied multisample count");
        RenderTarget2D probe(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
        const int applied = probe.getMultiSampleCountProperty();
        if (applied <= 0)
        {
            boundary(std::string("D1 ") + kRendererName + " applied multisample count " +
                     std::to_string(applied) + " for a requested 4 -- no MSAA resolve to measure");
            return;
        }
        boundary(std::string("D1 ") + kRendererName + " applied multisample count " +
                 std::to_string(applied));

        // (a) unbound first: the resolve is REQUIRED and its result must be exact.
        if (!kMsaaResolveReadable)
        {
            boundary(std::string("D1a ") + kRendererName + " cannot read a resolved MSAA render "
                     "target back -- the resolve CONTROL is declared, not measured");
        }
        else
        {
            step("D1a: MSAA target, flat Clear, unbind (which must resolve), read back");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            dev.Clear(kFlat);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback ra = ReadWholeTarget(a, kPW, kPH);
            if (Readable(ra, "D1a"))
                CheckFlat(ra, "D1a an MSAA target unbound before release is resolved exactly", kFlat);
        }

        // (b) released while bound, with the resolve still pending.
        {
            step("D1b: MSAA target, flat Clear, released WHILE BOUND with the resolve pending");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            dev.Clear(kFlat);
            Blit(dev, patternTex_, kPW, kPH);
        }
        RequireNextTargetExact(dev, "D1b");
    }

    /**
     * @brief Leg E1 -- mipmaps. Mip regeneration is the other piece of pending finalization.
     *
     * (a) proves the mip chain IS produced by the unbind, using a flat colour so level 1 is
     * byte-comparable without modelling the box filter. (b) releases a mipmapped target while it is
     * bound, i.e. with that regeneration still owed.
     */
    void LegE1(GraphicsDevice& dev)
    {
        try
        {
            // (a) unbound first: level 1 must carry the resolved, mip-reduced content.
            {
                step("E1a: mipmapped target, flat Clear, unbind (which must regenerate mips)");
                RenderTarget2D a(dev, kPW, kPH, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(kFlat);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                Readback l0 = ReadWholeTarget(a, kPW, kPH);
                if (Readable(l0, "E1a"))
                    CheckFlat(l0, "E1a a mipmapped target's level 0 is exact", kFlat);
                if (!kRenderTargetMipReadable)
                    boundary(std::string("E1a ") + kRendererName + " cannot read a render target's "
                             "mip level 1 back -- the regeneration CONTROL is declared, not measured");
                else
                {
                    Readback l1 = ReadTargetLevel(a, 1, kPW / 2, kPH / 2);
                    if (Readable(l1, "E1a level 1"))
                        CheckFlat(l1, "E1a the unbind regenerated level 1", kFlat);
                }
            }

            // (b) released while bound, with the mip regeneration still owed.
            {
                step("E1b: mipmapped target, flat Clear, released WHILE BOUND with mips still owed");
                RenderTarget2D a(dev, kPW, kPH, true, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(kFlat);
                Blit(dev, patternTex_, kPW, kPH);
            }
        }
        catch (const std::exception& e)
        {
            // WebGPU refuses mipMap=true on a render target outright, with a std::runtime_error
            // naming plans/plan_webgpu.md WEBGPU-53/54 -- deliberately, so the XNA layer's promised mip
            // chain cannot silently be missing. A renderer that SAYS so is declared here; one that
            // silently returns zeros is not, which is what kRenderTargetMipReadable exists for.
            if (!MipRefusal(e)) throw;
            boundary(std::string("E1 ") + kRendererName + " has no mipmapped render-target path (" +
                     e.what() + ") -- declared, not measured");
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            return;
        }
        RequireNextTargetExact(dev, "E1b");
    }

    /**
     * @brief Leg F1 -- a RenderTargetCube destroyed while one of its faces is the bound destination.
     *
     * A cube face has its own native attachment metadata (its own multisample renderbuffer, its own
     * resolve re-attachment and its own `lastFace_`), so it cannot be assumed to behave like an
     * ordinary RenderTarget2D. The oracle is that a SECOND cube, created after the first died
     * mid-binding, still renders and reads back every one of its six faces exactly.
     */
    void LegF1(GraphicsDevice& dev)
    {
        if (!kRenderTargetCubeSupported)
        {
            boundary(std::string("F1 ") + kRendererName + " has no render-into-a-cube path -- "
                     "declared, not measured");
            return;
        }
        constexpr int kCS = 4;
        try
        {
            {
                step("F1: create cube A, render several faces, leave face 3 BOUND");
                RenderTargetCube a(dev, kCS, false, SurfaceFormat::Color, DepthFormat::None);
                for (int face = 0; face < 4; ++face)
                {
                    dev.SetRenderTarget(&a, static_cast<CubeMapFace>(face));
                    ResetState(dev);
                    dev.Clear(Color(static_cast<bytecs>(30 + face * 30), bytecs(90), bytecs(220),
                                    bytecs(255)));
                }
                step("F1: cube A's DESTRUCTOR runs here, with face 3 still bound");
            }
            step("F1: SetRenderTarget(nullptr) after the cube died mid-binding");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            step("F1: create cube B and render all six faces");
            RenderTargetCube b(dev, kCS, false, SurfaceFormat::Color, DepthFormat::None);
            for (int face = 0; face < 6; ++face)
            {
                dev.SetRenderTarget(&b, static_cast<CubeMapFace>(face));
                ResetState(dev);
                dev.Clear(Color(static_cast<bytecs>(25 + face * 35), bytecs(140), bytecs(75),
                                bytecs(255)));
            }
            step("F1: SetRenderTarget(nullptr) and read every face of cube B back");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            int exactFaces = 0;
            std::string firstBad;
            bool readable = true;
            for (int face = 0; face < 6; ++face)
            {
                std::vector<Color> px(static_cast<std::size_t>(kCS) * kCS,
                                      Color(0xCD, 0xCD, 0xCD, 0xCD));
                try
                {
                    b.GetData(static_cast<CubeMapFace>(face), px.data(), 0,
                              static_cast<int>(px.size()));
                }
                catch (const System::NotSupportedException& e)
                {
                    boundary(std::string("F1 ") + kRendererName +
                             " cannot read a rendered cube face back (" + e.what() +
                             ") -- process outcome only");
                    readable = false;
                    break;
                }
                const Color want(static_cast<bytecs>(25 + face * 35), bytecs(140), bytecs(75),
                                 bytecs(255));
                bool allGood = true;
                for (const Color& c : px) if (!Same(c, want)) { allGood = false; break; }
                if (allGood) ++exactFaces;
                else if (firstBad.empty())
                    firstBad = " face " + std::to_string(face) + " want " + ColorText(want) +
                               " got " + ColorText(px[0]);
            }
            if (readable)
                check(exactFaces == 6,
                      "F1 a cube created after one died mid-binding renders all six faces exactly (" +
                      std::to_string(exactFaces) + "/6)" + firstBad);
        }
        catch (const System::NotSupportedException& e)
        {
            boundary(std::string("F1 ") + kRendererName + " has no RenderTargetCube path (" +
                     e.what() + ") -- declared, not measured");
            return;
        }
        RequireBackbufferExact(dev, "F1");
    }

    /**
     * @brief Leg P1 -- what the PUBLIC contract already does about a bound target at a frame end.
     *
     * This leg is why legs G1/G2/J1 look the way they do. `GraphicsDevice::Present()` refuses to
     * present while a render target is bound, and it decides that from `renderTargetBound_` -- a
     * BOOL, set by SetRenderTarget and untouched by any destructor. So the refusal is identical
     * whether the bound target is alive or already destroyed: it is a thrown
     * InvalidOperationException, never a dereference. That bounds the whole defect to the window
     * between the destructor and the next SetRenderTarget, because a frame boundary cannot be
     * crossed with a target bound at all -- and it is asserted here rather than assumed, since a
     * future renderer-level Present that consulted the binding OBJECT instead of the flag would turn
     * this leg red.
     */
    void LegP1(GraphicsDevice& dev)
    {
        {
            step("P1: bind a LIVE target and call Present() -- the public layer must refuse");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
            bool refusedAlive = false;
            try { dev.Present(); }
            catch (const System::InvalidOperationException&) { refusedAlive = true; }
            check(refusedAlive, "P1 Present() while a LIVE target is bound throws "
                                "InvalidOperationException");
            step("P1: A's DESTRUCTOR runs here, still bound");
        }
        step("P1: Present() again, now with the bound target DESTROYED");
        bool refusedDead = false;
        try { dev.Present(); }
        catch (const System::InvalidOperationException&) { refusedDead = true; }
        check(refusedDead, "P1 Present() while a DESTROYED target is bound throws the same "
                           "refusal, not a dereference");
        RequireBackbufferExact(dev, "P1");
    }

    /**
     * @brief Leg G1 (frame 1) -- the destroyed-while-bound transition, then a real frame boundary.
     *
     * The transition itself has to happen inside this frame: `Present()` refuses to run with a
     * target bound (leg P1), so the sequence cannot cross a frame boundary with the binding still
     * standing. What this leg adds over B1 is what comes AFTER -- a genuine Present and buffer swap
     * behind that transition, whose state frame 2 then measures.
     */
    void LegG1Frame1(GraphicsDevice& dev)
    {
        step("G1: create A, draw into it, release it WHILE BOUND, then transition");
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
        }
        BeginBackbuffer(dev);
        Consume3D(dev, &patternTex_);
        step("G1: returning from Draw -- a real Present and buffer swap now follow");
    }

    /** @brief Leg G1 (frame 2) -- the frame behind that Present must render exactly. */
    void LegG1Frame2(GraphicsDevice& dev)
    {
        step("G1: the frame after the swap must still produce and read a target exactly");
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, b, altPatternTex_);
        Readback rb = ReadWholeTarget(b, kPW, kPH);
        if (Readable(rb, "G1"))
            CheckExact(rb, "G1 a target produced in the frame after the swap is exact",
                       AltPatternColor);
        RequireBackbufferExact(dev, "G1");
    }

    /**
     * @brief Leg G2 (frame 1) -- an ordinary produce/unbind, so a real Present separates the frames.
     *
     * The target is deliberately kept ALIVE across the frame boundary but not bound, which is the
     * only shape the public contract allows. Its release, while re-bound, happens in frame 2.
     */
    void LegG2Frame1(GraphicsDevice& dev)
    {
        step("G2: create A, produce into it, unbind, and keep it alive across Present");
        acrossFrames_ = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                          DepthFormat::None, 0,
                                                          RenderTargetUsage::PreserveContents);
        ProduceInto(dev, *acrossFrames_, patternTex_);
    }

    /** @brief Leg G2 (frame 2) -- re-bound one Present later, then released while bound. */
    void LegG2Frame2(GraphicsDevice& dev)
    {
        step("G2: re-bind the target created LAST frame, draw into it, release it while bound");
        dev.SetRenderTarget(acrossFrames_.get());
        ResetState(dev);
        Blit(dev, altPatternTex_, kPW, kPH);
        acrossFrames_.reset();
        RequireBackbufferExact(dev, "G2");
    }

    /** @brief Leg H1 -- A -> B -> A alternation, with A finally released while bound. */
    void LegH1(GraphicsDevice& dev)
    {
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::PreserveContents);
        {
            step("H1: A -> B -> A, then release A while it is bound the second time");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
            FillBoundTarget(dev, b, altPatternTex_);
            dev.SetRenderTarget(&a);
            ResetState(dev);
            dev.Clear(kFlat);
        }
        step("H1: SetRenderTarget(nullptr)");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        step("H1: read B, which was bound in the middle of the alternation");
        Readback rb = ReadWholeTarget(b, kPW, kPH);
        if (Readable(rb, "H1"))
            CheckExact(rb, "H1 the target bound between two bindings of A is still exact",
                       AltPatternColor);
        RequireBackbufferExact(dev, "H1");
    }

    /**
     * @brief Leg I1 -- a new target constructed straight onto the freed one's storage.
     *
     * A binding recorded as a bare object ADDRESS cannot tell a dead target from a live one that the
     * allocator happened to place at the same address. The public wrapper's address is what this leg
     * can observe and report; the renderer object's address is recorded by CNA_EASYGL_TARGET_TRACE.
     */
    void LegI1(GraphicsDevice& dev)
    {
        const void* firstAddress = nullptr;
        {
            step("I1: create A, draw into it, release it WHILE BOUND");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            firstAddress = static_cast<const void*>(&a);
            FillBoundTarget(dev, a, patternTex_);
        }
        step("I1: construct a NEW target immediately, hoping for the freed address");
        auto b = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                  DepthFormat::None, 0,
                                                  RenderTargetUsage::DiscardContents);
        boundary(std::string("I1 first wrapper ") + (firstAddress == static_cast<const void*>(b.get())
                                                     ? "address REUSED by the replacement"
                                                     : "address not reused by the replacement"));
        step("I1: produce into the replacement and read it back");
        ProduceInto(dev, *b, altPatternTex_);
        Readback rb = ReadWholeTarget(*b, kPW, kPH);
        if (Readable(rb, "I1"))
            CheckExact(rb, "I1 a target built after a bound one died is exact", AltPatternColor);
        RequireBackbufferExact(dev, "I1");
    }

    /**
     * @brief Leg J1 -- device teardown immediately behind a destroyed-while-bound transition.
     *
     * `heldBound_` is a member of this class, so it is destroyed BEFORE `Game`'s own
     * `GraphicsDevice_` member; the reverse order -- a render target outliving its graphics renderer
     * -- is therefore not reachable through this harness at all, and is handled by construction
     * rather than measured here (see the record). What this leg does measure is the reachable half:
     * one target dies while bound, the transition runs, and a SECOND target is still alive and
     * bound-free when the device tears down one instruction later, with no further public frame to
     * absorb anything. The subject is the process outcome, which the supervisor judges.
     */
    void LegJ1(GraphicsDevice& dev)
    {
        {
            step("J1: create A, draw into it, and release it WHILE BOUND");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
        }
        step("J1: transition away from the destroyed target");
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        step("J1: leave a live target for teardown to destroy after the device's last frame");
        heldBound_ = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0,
                                                      RenderTargetUsage::DiscardContents);
        ProduceInto(dev, *heldBound_, patternTex_);
        Readback rh = ReadWholeTarget(*heldBound_, kPW, kPH);
        if (Readable(rh, "J1"))
            CheckExact(rh, "J1 the target handed to device teardown is exact first", PatternColor);
        check(true, "J1 a destroyed-while-bound transition immediately before teardown completes");
    }

    /** @brief Leg K1 -- an exception unwinds the scope, destroying the bound target on the way out. */
    void LegK1(GraphicsDevice& dev)
    {
        try
        {
            step("K1: bind A, draw into it, then throw while A is still bound");
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            FillBoundTarget(dev, a, patternTex_);
            throw std::runtime_error("K1 deliberate throw with a bound render target");
        }
        catch (const std::runtime_error&)
        {
            check(true, "K1 unwinding past a bound render target reaches the handler");
        }
        RequireBackbufferExact(dev, "K1");
    }

    /**
     * @brief Leg L1 -- one slot of a bound MULTI-target set destroyed while the set is bound.
     *
     * This is the one binding shape where "the dying target's finalization has no observer" does NOT
     * extend to the whole set: slot 1 is a live, mipmapped target whose mip regeneration is owed and
     * publicly readable. A detach that abandoned the SET rather than the SLOT would lose it.
     */
    void LegL1(GraphicsDevice& dev)
    {
        std::unique_ptr<RenderTarget2D> survivor;
        try
        {
            survivor = std::make_unique<RenderTarget2D>(dev, kPW, kPH, true, SurfaceFormat::Color,
                                                        DepthFormat::None, 0,
                                                        RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            if (!MipRefusal(e)) throw;
            boundary(std::string("L1 ") + kRendererName + " has no mipmapped render-target path (" +
                     e.what() + ") -- declared, not measured");
            return;
        }
        try
        {
            step("L1: bind a 2-slot MRT set and Clear it");
            {
                RenderTarget2D dying(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                     RenderTargetUsage::DiscardContents);
                dev.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&dying)),
                                      RenderTargetBinding(static_cast<Texture*>(survivor.get()))});
                ResetState(dev);
                dev.Clear(kFlatAlt);
                step("L1: slot 0's DESTRUCTOR runs here, with the whole set still bound");
            }
            step("L1: SetRenderTarget(nullptr) -- the surviving slot must still be finalized");
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
        }
        catch (const std::exception& e)
        {
            boundary(std::string("L1 ") + kRendererName + " has no usable 2-slot MRT path (" +
                     e.what() + ") -- declared, not measured");
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
            return;
        }
        step("L1: read the survivor's level 0 and level 1");
        Readback l0 = ReadWholeTarget(*survivor, kPW, kPH);
        if (Readable(l0, "L1"))
            CheckFlat(l0, "L1 the surviving MRT slot's level 0 is exact", kFlatAlt);
        if (!kMrtSlotMipReadable)
            boundary(std::string("L1 ") + kRendererName + " cannot read an MRT member's mip level 1 "
                     "back -- the survivor's regeneration is declared, not measured");
        else
        {
            Readback l1 = ReadTargetLevel(*survivor, 1, kPW / 2, kPH / 2);
            if (Readable(l1, "L1 level 1"))
                CheckFlat(l1, "L1 the surviving MRT slot's mip chain was still regenerated",
                          kFlatAlt);
        }
        RequireBackbufferExact(dev, "L1");
    }

    /**
     * @brief Leg M1 -- 120 create / bind / destroy-while-bound cycles.
     *
     * Enough rounds that GL reuses framebuffer and texture NAMES many times over (it hands out the
     * lowest free name), so a cached handle or a stale binding record that survived a round would
     * alias a live resource here rather than merely dangling. The oracle is that the last round's
     * content, the next target and the backbuffer are all still exact.
     */
    void LegM1(GraphicsDevice& dev)
    {
        constexpr int kRounds = 120;
        /// The fewest rounds that still forces GL/native name reuse many times over. A renderer with a
        /// documented per-FRAME resource cap (bgfx runs out of ordered view-segment ids past ~190
        /// segments, REMED-GFX-065) cannot reach 120 in one frame; below this many, the leg would not
        /// be measuring repetition at all and is a failure rather than a boundary.
        constexpr int kMinRounds = 8;
        step("M1: 120 rounds of create / bind / draw / destroy-while-bound");
        int completed = 0;
        try
        {
            for (int round = 0; round < kRounds; ++round)
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(kFlat);
                Blit(dev, patternTex_, kPW, kPH);
                // No transition: the destructor at the end of this iteration is the only unbind.
                ++completed;
            }
        }
        catch (const std::exception& e)
        {
            boundary(std::string("M1 ") + kRendererName + " refused round " +
                     std::to_string(completed + 1) + " (" + e.what() + ") -- " +
                     std::to_string(completed) + " rounds measured");
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        }
        check(completed >= kMinRounds, "M1 " + std::to_string(completed) +
                                       " destroy-while-bound rounds completed");
        if (completed < kRounds)
        {
            // A renderer with a per-FRAME resource cap has nothing left in this frame, so its closing
            // oracle cannot run either. Declared rather than retried in a second frame: another frame
            // would move the subject from "state after N rounds of churn" to "state after a Present",
            // which is leg G1's subject and not this one's.
            boundary(std::string("M1 ") + kRendererName + " has no frame budget left after " +
                     std::to_string(completed) + " rounds -- the closing content oracle is declared, "
                     "not measured");
            return;
        }
        step("M1: a final target must still be exact after all that name churn");
        RenderTarget2D last(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                            RenderTargetUsage::DiscardContents);
        ProduceInto(dev, last, patternTex_);
        Readback rl = ReadWholeTarget(last, kPW, kPH);
        if (Readable(rl, "M1"))
            CheckExact(rl, "M1 a target created after 120 rounds of name churn is exact",
                       PatternColor);
        RequireBackbufferExact(dev, "M1");
    }

    /**
     * @brief Leg N1 -- two live targets, released in the REVERSE order, one of them bound.
     *
     * A detach that walked a list, or that assumed the dying target is always the current one, would
     * only show up with more than one target alive at the moment of death.
     */
    void LegN1(GraphicsDevice& dev)
    {
        auto first = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0,
                                                      RenderTargetUsage::DiscardContents);
        auto second = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0,
                                                        RenderTargetUsage::DiscardContents);
        step("N1: produce into both, leave the SECOND bound, then release the FIRST");
        ProduceInto(dev, *first, patternTex_);
        FillBoundTarget(dev, *second, altPatternTex_);
        first.reset();   // <-- not the bound one: the detach must leave the binding alone
        step("N1: the still-bound second target must keep receiving work");
        Blit(dev, patternTex_, kPW, kPH);
        step("N1: now release the BOUND one too");
        second.reset();
        RequireBackbufferExact(dev, "N1");
    }

    // ---------------------------------------------------------------- driver

    void Draw(const GameTime& /*gameTime*/) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();

        // The two multi-frame legs need a real Present between their halves, so they own the frame
        // loop rather than sharing frame 1 with everything else.
        if (onlyLeg_ == "G1")
        {
            if (frame_ == 1) { LegG1Frame1(dev); return; }
            LegG1Frame2(dev);
            Finish();
            return;
        }
        if (onlyLeg_ == "G2")
        {
            if (frame_ == 1) { LegG2Frame1(dev); return; }
            LegG2Frame2(dev);
            Finish();
            return;
        }

        auto runLeg = [&](const char* id, void (BoundTargetLifetimeTest::*leg)(GraphicsDevice&))
        {
            if (!wants(id)) return;
            try { (this->*leg)(dev); }
            catch (const std::exception& e)
            {
                check(false, std::string("leg ") + id + ": an unexpected exception escaped: " + e.what());
            }
            catch (...)
            {
                check(false, std::string("leg ") + id + ": an unexpected non-std exception escaped");
            }
            // A leg that threw mid-bind must not leave a target bound for the next one. J1's
            // subject IS a target left bound, so it is excluded.
            if (std::string(id) != "J1")
                try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        };

        runLeg("A1", &BoundTargetLifetimeTest::LegA1);
        runLeg("A2", &BoundTargetLifetimeTest::LegA2);
        runLeg("B1", &BoundTargetLifetimeTest::LegB1);
        runLeg("B2", &BoundTargetLifetimeTest::LegB2);
        runLeg("C1", &BoundTargetLifetimeTest::LegC1);
        runLeg("D1", &BoundTargetLifetimeTest::LegD1);
        runLeg("E1", &BoundTargetLifetimeTest::LegE1);
        runLeg("F1", &BoundTargetLifetimeTest::LegF1);
        runLeg("H1", &BoundTargetLifetimeTest::LegH1);
        runLeg("I1", &BoundTargetLifetimeTest::LegI1);
        runLeg("K1", &BoundTargetLifetimeTest::LegK1);
        runLeg("L1", &BoundTargetLifetimeTest::LegL1);
        runLeg("M1", &BoundTargetLifetimeTest::LegM1);
        runLeg("N1", &BoundTargetLifetimeTest::LegN1);
        runLeg("P1", &BoundTargetLifetimeTest::LegP1);
        // J1 last in the all-legs path: it deliberately hands a live target to teardown.
        runLeg("J1", &BoundTargetLifetimeTest::LegJ1);

        Finish();
    }

    void Finish()
    {
        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        // A leg may legitimately have nothing to measure on a renderer that lacks the capability --
        // but only when it SAID so. A run that measured nothing and explained nothing is a failure.
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        std::vector<std::uint8_t> pattern(static_cast<std::size_t>(kPW) * kPH * 4);
        std::vector<std::uint8_t> alt(static_cast<std::size_t>(kPW) * kPH * 4);
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const std::size_t o = (static_cast<std::size_t>(y) * kPW + x) * 4;
                const Color p = PatternColor(x, y);
                const Color a = AltPatternColor(x, y);
                pattern[o + 0] = p.getRProperty(); pattern[o + 1] = p.getGProperty();
                pattern[o + 2] = p.getBProperty(); pattern[o + 3] = p.getAProperty();
                alt[o + 0] = a.getRProperty(); alt[o + 1] = a.getGProperty();
                alt[o + 2] = a.getBProperty(); alt[o + 3] = a.getAProperty();
            }
        patternTex_    = Texture2D::CreateFromPixels(dev, kPW, kPH, pattern);
        altPatternTex_ = Texture2D::CreateFromPixels(dev, kPW, kPH, alt);
    }

public:
    explicit BoundTargetLifetimeTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        // Leg D1's subject is a MULTISAMPLED target, and on renderers that tie render-target MSAA to
        // the device's own sample count a render target can only engage it when the backbuffer was
        // created multisampled. Requested ONLY in D1's own child -- the supervisor runs one leg per
        // child -- so no other leg's measurement moves, and the in-process fallback keeps D1's
        // honest "nothing to measure" declaration.
        if (onlyLeg_ == "D1") gdm_->setPreferMultiSamplingProperty(true);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /** @brief Every leg id this fixture knows, in the order the supervisor runs them. */
    const char* const kLegIds[] = {
        "A1", "A2", "B1", "B2", "C1", "D1", "E1", "F1", "G1", "G2", "H1", "I1", "J1", "K1",
        "L1", "M1", "N1", "P1",
    };

#if CNA_GFX168_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever -- a modal dialog or a
    /// wedged device is a different finding from a crash and has to stay distinguishable.
    constexpr unsigned kLegTimeoutSeconds = 240;

    // Matches CNA::Examples::kSkipExitCode (common/PixelTestGame.hpp) -- a leg that exits with this
    // code decided for itself, before this supervisor is involved, that its environment can't
    // support the check (LLGL-55: e.g. the shared Vulkan-WSI-unavailable guard,
    // examples/common/LlglVulkanWsiSkipGuard.cpp). That is not the same as the leg failing, so it
    // must not be counted or reported as one.
    constexpr int kLegSkipExitCode = 77;

    /**
     * @brief Runs one leg in a child process and classifies the outcome.
     *
     * The pre-fix failure is a SIGSEGV, so a leg's result has four states, not two. Naming the
     * signal is what makes "the process died" an attributable measurement rather than a lost run,
     * and re-execing means one leg's crash cannot cost the remaining legs.
     *
     * @param exePath  This binary's own path.
     * @param legId    The leg to run.
     * @param crashed  Set to true when the child was killed by a signal.
     * @param skipped  Set to true when the child exited with kLegSkipExitCode.
     * @return True when the child exited 0.
     */
    bool RunLegIsolated(const char* exePath, const char* legId, bool& crashed, bool& skipped)
    {
        crashed = false;
        skipped = false;
        std::string arg = std::string("--leg=") + legId;

        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", legId);
            return false;
        }
        if (pid == 0)
        {
            alarm(kLegTimeoutSeconds);   // a hang arrives as SIGALRM, reported distinctly below
            char* argv[3];
            argv[0] = const_cast<char*>(exePath);
            argv[1] = const_cast<char*>(arg.c_str());
            argv[2] = nullptr;
            execv(exePath, argv);
            std::_Exit(127);
        }

        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }

        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            crashed = true;
            if (sig == SIGALRM)
                std::printf("[TIMEOUT] leg %s: no result within %u s (hang or modal dialog)\n",
                            legId, kLegTimeoutSeconds);
            else
                std::printf("[CRASH] leg %s: killed by signal %d (%s)%s\n", legId, sig,
                            strsignal(sig), WCOREDUMP(status) ? " (core dumped)" : "");
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == 0) return true;
            if (code == kLegSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: exited %d\n", legId, code);
                std::fflush(stdout);
                return false;
            }
            std::printf("[FAIL] leg %s: exited %d (checks ran and disagreed)\n", legId, code);
            std::fflush(stdout);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled (status %d)\n", legId, status);
        std::fflush(stdout);
        return false;
    }
#endif
}

int main(int argc, char** argv)
{
    std::string onlyLeg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind("--leg=", 0) == 0) onlyLeg = a.substr(6);
    }

#if CNA_GFX168_CAN_FORK
    if (onlyLeg.empty())
    {
        // Supervisor. Each leg runs in its own child so a SIGSEGV is a reported result rather than
        // the end of the run.
        std::printf("[INFO] REMED-GFX-168 supervisor: %zu legs, each in its own process\n",
                    sizeof(kLegIds) / sizeof(kLegIds[0]));
        std::fflush(stdout);
        int passed = 0, crashes = 0, skips = 0;
        const int total = static_cast<int>(sizeof(kLegIds) / sizeof(kLegIds[0]));
        for (const char* leg : kLegIds)
        {
            bool crashed = false, skipped = false;
            if (RunLegIsolated(argv[0], leg, crashed, skipped)) ++passed;
            if (crashed) ++crashes;
            if (skipped) ++skips;
        }
        std::printf("[INFO] supervisor: %d/%d legs passed, %d crashed, %d skipped\n",
                    passed, total, crashes, skips);
        std::fflush(stdout);
        const int failed = total - passed - skips;
        if (failed > 0) return 1;
        return (skips > 0) ? kLegSkipExitCode : 0;
    }
#endif

    BoundTargetLifetimeTest game(onlyLeg);
    game.Run();
    return game.Result();
}
