// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-154 -- the FIRST `GetData` of a MULTISAMPLED `RenderTarget2D` must return the complete
// resolved image, with no priming read, no extra public frame, no `Present`, no dummy target bind,
// no sleep and no retry.
//
// THE DEFECT
// ----------
// On BGFX the canonical public sequence
//
//     RenderTarget2D rt(dev, 16, 16, false, Color, DepthFormat::None, 4, DiscardContents);
//     dev.SetRenderTarget(&rt);
//     dev.Clear(base);            // nonzero, asymmetric
//     ... draw an asymmetric pattern ...
//     dev.SetRenderTarget(nullptr);
//     rt.GetData(dst, 0, 16 * 16);   // <-- exactly once, immediately
//
// returned every texel as `(0,0,0,0)` -- not the geometry and not even the clear colour. A SECOND
// `GetData` of the same target returned the exact image, which is what makes this a FIRST-read
// defect rather than a general readback defect, and what makes a "just read it twice" workaround so
// tempting. The non-multisampled target read in the same frame was always byte-exact.
//
// WHY: bgfx's GL renderer performs a framebuffer's multisample RESOLVE (`FrameBufferGL::resolve`,
// the `glBlitFramebuffer` from `m_fbo[0]`, the multisample renderbuffer, into `m_fbo[1]`, the
// single-sample texture) only from `setFrameBuffer()`, i.e. when the renderer switches AWAY from
// that framebuffer to a DIFFERENT one. The readback blit this renderer queues on the reserved
// highest view id is drained by the TAIL `submitBlit(bs, BGFX_CONFIG_MAX_VIEWS)` at the end of
// `RendererContextGL::submit`, which runs while the producer's framebuffer is still current and
// still unresolved -- so `glCopyImageSubData` copies the resolve texture that nothing has written
// yet. That texture is freshly created and zeroed, so the readback SUCCEEDS over untouched memory:
// exactly the fabricated transparent-black frame REMED-GFX-127/130 exist to forbid.
//
// The CUBE path already carried `CompleteFrameForResolveEXT()` for precisely this reason
// (REMED-GFX-134); its 2D sibling never learned it -- the same "the fix exists one screen away"
// shape REMED-GFX-163 had just closed on the attachment-creation side of the same class.
//
// RELATIONSHIP TO REMED-GFX-164
// -----------------------------
// GFX-164 originally recorded all-zero MSAA readback on EasyGL AND Bgfx. The Bgfx half is this
// ticket: same unbound public sequence, same target, same all-zero result, same source handle, and
// the two are NOT independently reproducible. GFX-154 is canonical for Bgfx. GFX-164 retained the
// distinct EasyGL sequence in which GetData reads the target while it is still bound; that active
// producer required its own resolve correction. This fixture remains the cross-renderer control for
// the unbound first-read contract and fully asserts EasyGL content as well as transfer boundaries.
//
// THE ORACLE
// ----------
// Every read writes into a SENTINEL-prefilled destination: a protected prefix, the requested
// window, and a protected suffix, all initialised to a colour that appears nowhere in the rendered
// image. That makes three distinct failures separable which a bare `std::vector<Color>` (which
// zero-initialises, i.e. to exactly the defect's own output) cannot tell apart:
//   * a readback that wrote nothing            -> the window still holds the sentinel;
//   * a readback that wrote zeroes             -> the window holds (0,0,0,0), which no rendered
//                                                 texel is: every oracle colour has all four
//                                                 channels nonzero on purpose;
//   * a readback that wrote outside its window -> the prefix or the suffix changed.
//
// PROCESS ISOLATION
// -----------------
// Each leg runs in its own forked child so a native abort in one cannot destroy the other results,
// and so "first read of the PROCESS" is a property this file can actually control. The supervisor
// names SIGTRAP, SIGABRT, SIGSEGV and SIGALRM separately. No leg is permitted to abort.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX154_CAN_FORK 1
#else
#define CNA_GFX154_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRT  = 16;   ///< Canonical render-target edge; quadrants are 8x8.
    constexpr int kBBW = 64;   ///< Backbuffer width. Nothing in this file reads the backbuffer.
    constexpr int kBBH = 64;   ///< Backbuffer height.

#if defined(CNA_RENDERER_HEADLESS)
    constexpr const char* kRendererName = "HEADLESS";
    constexpr bool kRasterizes = false;
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_BGFX)
    constexpr const char* kRendererName = "BGFX";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_VULKAN)
    constexpr const char* kRendererName = "VULKAN";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr const char* kRendererName = "WEBGPU";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr const char* kRendererName = "SDL_GPU";
    constexpr bool kRasterizes = true;
#else
#error "REMED-GFX-154: this renderer has no declared first-readback contract."
#endif

    /**
     * @brief Whether a MULTISAMPLED render target's CONTENT is asserted on this renderer.
     *
     * TRUE EVERYWHERE, INCLUDING EASYGL -- and that is a measurement, not an assumption.
     *
     * This started as an EasyGL boundary, because REMED-GFX-164 recorded an all-zero multisampled
     * readback there and REMED-GFX-154 changes Bgfx production only. The boundary was written to
     * PRINT the texels it measured rather than skip silently, exactly so a stale declaration would
     * be visible -- and it was: EasyGL measured **0/256 all-zero texels with the centre reading the
     * correct (250,25,15,255)**. Asserting the content there instead gives **34/34 legs and 145/145
     * checks**, so EasyGL's multisampled readback is byte-exact through THIS public sequence.
     * REMED-GFX-164 subsequently proved that its own failure required the distinct active-target
     * sequence: the unresolved public texture lagged the live multisample renderbuffer until
     * unbind. The active resolve correction leaves this unbound cross-renderer contract unchanged.
     */
    constexpr bool kMsaaContentReadable = true;

    /**
     * @brief Whether this renderer can read a render target's colour attachment back at all.
     *
     * False on HEADLESS, which rasterizes nothing and therefore owns no colour to return. Its
     * `GetData` is REMED-GFX-127/130's deterministic public REFUSAL --
     * `System::NotSupportedException` -- rather than a fabricated transparent-black frame, and that
     * refusal is exactly what this file asserts there: it must throw, and it must leave the
     * caller's destination completely untouched. A refusal that quietly wrote zeroes would be
     * indistinguishable from the defect this whole ticket is about.
     */
    constexpr bool kTargetReadbackSupported =
#if defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    /**
     * @brief Whether probing mip level 1 of a mipmapped MULTISAMPLED target is survivable here.
     *
     * TRUE EVERYWHERE, INCLUDING SDL_GPU -- and that is now a measurement rather than a hope.
     *
     * This was false on SDL_GPU, which SEGFAULTED on `GetData(1, rect, ...)` of a
     * `RenderTarget2D(16,16,mipMap=true,MSAA=4)` while level 0 of the very same target read back
     * exactly. REMED-GFX-154 isolated that, recorded it as **REMED-GFX-186** and declared it here
     * rather than issuing an uncatchable signal from a fixture scoped to Bgfx production.
     * REMED-GFX-186 has since fixed it -- SDL_GPU's multisampled target now carries the full
     * declared mip chain on its resolved single-sample texture, and an out-of-range level is a
     * catchable public exception -- so the carve-out is gone and leg G1 issues the read on every
     * renderer again.
     */
    constexpr bool kMsaaMipLevelProbeSafe = true;

    /**
     * @brief Whether a render target may be built with a mip chain here.
     *
     * True everywhere. It was false on WEBGPU while that renderer's `RenderTarget2D`
     * constructor raised a catchable `std::runtime_error` for `mipMap=true`;
     * `plans/plan_webgpu.md` `WEBGPU-164` allocates the chain and regenerates it from level 0
     * on unbind, so the legs below measure a value there rather than a refusal.
     */
    constexpr bool kMipMappedTargetSupported = true;

    // ---- the asymmetric pattern -------------------------------------------------------------
    //
    // Unique corners, distinct rows AND columns, one mid-tone, nonzero alpha, and -- deliberately
    // -- every channel of every colour nonzero, so a single zeroed channel is a visible failure
    // rather than something a tolerance could absorb. Any two differ by >= 90 on some channel,
    // far outside kTol, so a swapped quadrant can never read as a rounding miss.
    const Color kBase(20, 60, 140, 255);    ///< Clear colour; also the bottom-right quadrant.
    const Color kTL  (250, 25, 15, 255);    ///< Top-left quadrant.
    const Color kTR  (15, 245, 30, 255);    ///< Top-right quadrant.
    const Color kBL  (120, 130, 125, 255);  ///< Bottom-left quadrant -- the mid-tone.

    /// Absent from the rendered image on every channel, and distinct from (0,0,0,0).
    const Color kSentinel(7, 3, 11, 199);

    /// Vertex-colour round trips differ by a unit or two across renderers and MSAA resolves.
    constexpr int kTol = 14;

    /// Protected elements written before and after every requested destination window.
    constexpr int kGuard = 5;

    bool Near(const Color& got, const Color& want)
    {
        auto d = [](int a, int b) { return a > b ? a - b : b - a; };
        return d(got.getRProperty(), want.getRProperty()) <= kTol
            && d(got.getGProperty(), want.getGProperty()) <= kTol
            && d(got.getBProperty(), want.getBProperty()) <= kTol
            && d(got.getAProperty(), want.getAProperty()) <= kTol;
    }

    bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /** @brief The colour the asymmetric pattern puts at (@p x, @p y) of a @p w x @p h target. */
    Color Expected(int x, int y, int w, int h)
    {
        const bool left = x < w / 2;
        const bool top  = y < h / 2;
        if (top && left)  return kTL;
        if (top && !left) return kTR;
        if (!top && left) return kBL;
        return kBase;
    }

    /**
     * @brief True when (@p x, @p y) is far enough from a quadrant SEAM to be free of MSAA blend.
     *
     * Only the two interior seams (x = w/2, y = h/2) are excluded. The target's OUTER border is
     * deliberately NOT excluded: every quad spans its half of NDC exactly, so the outermost pixel
     * row and column are fully covered and resolve to the flat quadrant colour. Excluding them too
     * -- the first spelling of this predicate -- made the `final row` rectangle check vacuous,
     * because every texel in row h-1 was skipped and a check with nothing to compare passes.
     */
    bool Interior(int x, int y, int w, int h)
    {
        const int dx = x < w / 2 ? (w / 2 - 1 - x) : (x - w / 2);
        const int dy = y < h / 2 ? (h / 2 - 1 - y) : (y - h / 2);
        return dx >= 2 && dy >= 2;
    }
}

/**
 * @brief REMED-GFX-154's contract: the first synchronous readback of a multisampled render target.
 */
class RenderTargetMsaaFirstReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Leg F2's target: read once, then handed to device teardown still alive.
    std::unique_ptr<RenderTarget2D> heldToTeardown_;

    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;
    bool boundaryDeclared_ = false;
    std::string onlyLeg_;

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

    /** @brief Names the public operation about to be issued, flushed, so an abort is locatable. */
    void step(const std::string& text) const
    {
        std::printf("[STEP] %s\n", text.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    // ---------------------------------------------------------------- the producer

    /** @brief A clockwise NDC quad in one flat colour, surviving `CullCounterClockwiseFace`. */
    static void FillQuad(VertexPositionColor* o, const Color& c,
                         float x0, float y0, float x1, float y1)
    {
        o[0] = { Vector3(x0, y1, 0.f), c };
        o[1] = { Vector3(x1, y0, 0.f), c };
        o[2] = { Vector3(x0, y0, 0.f), c };
        o[3] = { Vector3(x0, y1, 0.f), c };
        o[4] = { Vector3(x1, y1, 0.f), c };
        o[5] = { Vector3(x1, y0, 0.f), c };
    }

    static void DrawFlatQuad(GraphicsDevice& dev, const Color& c,
                             float x0, float y0, float x1, float y1)
    {
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(false);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        VertexPositionColor v[6];
        FillQuad(v, c, x0, y0, x1, y1);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, v, 0, 2);
    }

    /**
     * @brief Binds @p rt, clears it to kBase and draws the three-quadrant asymmetric pattern.
     *
     * Leaves the target UNBOUND on return -- the caller's next public call is the `GetData` under
     * test, with nothing between them.
     */
    void RenderPattern(GraphicsDevice& dev, RenderTarget2D& rt, bool clear = true,
                       bool geometry = true)
    {
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        dev.SetRenderTarget(&rt);
        if (clear) dev.Clear(kBase);
        if (geometry)
        {
            DrawFlatQuad(dev, kTL, -1.f,  0.f,  0.f,  1.f);
            DrawFlatQuad(dev, kTR,  0.f,  0.f,  1.f,  1.f);
            DrawFlatQuad(dev, kBL, -1.f, -1.f,  0.f,  0.f);
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // ---------------------------------------------------------------- the sentinel oracle

    /**
     * @brief A destination of `kGuard + count + kGuard` elements, every one the sentinel colour.
     */
    static std::vector<Color> SentinelBuffer(int count)
    {
        return std::vector<Color>(static_cast<std::size_t>(count) + 2 * kGuard, kSentinel);
    }

    /**
     * @brief Runs @p doRead, or asserts this renderer's deterministic refusal instead.
     *
     * On a renderer that cannot read a render target back, the ONLY correct behaviour is a public
     * exception that writes nothing -- so that is what gets asserted, using the same sentinel
     * buffer. A refusal that silently wrote zeroes would look exactly like REMED-GFX-154 itself.
     *
     * @return true when the read really happened and @p buf may be inspected.
     */
    template <typename F>
    bool IssueRead(F&& doRead, const std::vector<Color>& buf, const std::string& label)
    {
        if (kTargetReadbackSupported)
        {
            doRead();
            return true;
        }
        bool threw = false;
        try { doRead(); }
        catch (const std::exception&) { threw = true; }
        check(threw, label + ": this renderer REFUSES render-target readback, and does so through a "
                             "catchable public exception");
        bool untouched = true;
        for (const Color& c : buf)
            if (!Exact(c, kSentinel)) { untouched = false; break; }
        check(untouched, label + ": the refused read left the whole destination untouched");
        return false;
    }

    /**
     * @brief Asserts the protected prefix and suffix around a `count`-element window are untouched.
     */
    bool GuardsIntact(const std::vector<Color>& buf, int count, const std::string& label)
    {
        bool ok = true;
        for (int i = 0; i < kGuard; ++i)
            if (!Exact(buf[static_cast<std::size_t>(i)], kSentinel))
            {
                std::printf("        prefix element %d = %s, expected sentinel %s\n", i,
                            ColorText(buf[static_cast<std::size_t>(i)]).c_str(),
                            ColorText(kSentinel).c_str());
                ok = false;
                break;
            }
        for (int i = 0; i < kGuard; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(kGuard + count + i);
            if (!Exact(buf[idx], kSentinel))
            {
                std::printf("        suffix element %d = %s, expected sentinel %s\n", i,
                            ColorText(buf[idx]).c_str(), ColorText(kSentinel).c_str());
                ok = false;
                break;
            }
        }
        check(ok, label + ": protected prefix and suffix are untouched");
        return ok;
    }

    /**
     * @brief Compares a readback window against the asymmetric pattern, texel by texel.
     *
     * @param buf   the sentinel buffer; the window starts at kGuard.
     * @param rx,ry the requested rectangle's origin inside the target.
     * @param rw,rh the requested rectangle's size.
     * @param tw,th the full target's size (the pattern is defined over this).
     * @param stats out: how many texels were still the sentinel, and how many were exactly zero.
     */
    bool WindowMatches(const std::vector<Color>& buf, int rx, int ry, int rw, int rh,
                       int tw, int th, int& untouched, int& zeroed, std::string& firstMismatch)
    {
        untouched = 0;
        zeroed = 0;
        bool ok = true;
        const Color black(0, 0, 0, 0);
        for (int row = 0; row < rh; ++row)
        {
            for (int col = 0; col < rw; ++col)
            {
                const Color got = buf[static_cast<std::size_t>(kGuard + row * rw + col)];
                if (Exact(got, kSentinel)) ++untouched;
                if (Exact(got, black)) ++zeroed;
                const int x = rx + col;
                const int y = ry + row;
                if (!Interior(x, y, tw, th)) continue;   // MSAA blends the quadrant seams
                const Color want = Expected(x, y, tw, th);
                if (!Near(got, want))
                {
                    if (ok)
                        firstMismatch = "(" + std::to_string(x) + "," + std::to_string(y) +
                                        ") got " + ColorText(got) + " want " + ColorText(want);
                    ok = false;
                }
            }
        }
        return ok;
    }

    /**
     * @brief The whole first-read contract for one target, in one call.
     *
     * Prefills a sentinel destination, issues EXACTLY ONE `GetData`, and asserts: every requested
     * element was overwritten, the guards survived, and the window holds the resolved pattern. On a
     * renderer where multisampled CONTENT is a declared boundary the last of those three becomes a
     * printed measurement instead of a failure -- the other two stay assertions.
     *
     * @return the texels read, for a differential comparison by the caller.
     */
    std::vector<Color> ReadAndAssert(RenderTarget2D& rt, const std::string& label,
                                     bool contentAsserted = true)
    {
        const int w = rt.getWidthProperty();
        const int h = rt.getHeightProperty();
        const int count = w * h;
        std::vector<Color> buf = SentinelBuffer(count);

        step(label + ": GetData(dst, " + std::to_string(kGuard) + ", " + std::to_string(count) + ")");
        if (!IssueRead([&] { rt.GetData(buf.data(), kGuard, count); }, buf, label))
            return std::vector<Color>(static_cast<std::size_t>(count), kSentinel);

        int untouched = 0, zeroed = 0;
        std::string firstMismatch;
        const bool matched = WindowMatches(buf, 0, 0, w, h, w, h, untouched, zeroed, firstMismatch);

        check(untouched == 0,
              label + ": every one of the " + std::to_string(count) +
                  " requested elements was written (" + std::to_string(untouched) +
                  " still held the sentinel)");
        GuardsIntact(buf, count, label);

        if (contentAsserted)
        {
            if (!matched)
                std::printf("        %d/%d texels are exactly (0,0,0,0); first mismatch %s\n",
                            zeroed, count, firstMismatch.c_str());
            check(matched, label + ": the window holds the complete resolved image");
        }
        else
        {
            boundary(label + ": multisampled CONTENT is not asserted on this renderer -- measured " +
                     std::to_string(zeroed) + "/" +
                     std::to_string(count) + " texels exactly (0,0,0,0), centre " +
                     ColorText(buf[static_cast<std::size_t>(kGuard + (h / 4) * w + (w / 4))]));
        }

        return std::vector<Color>(buf.begin() + kGuard, buf.begin() + kGuard + count);
    }

    /** @brief Builds the canonical multisampled target. */
    static std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, int w, int h,
                                                      int samples, DepthFormat depth,
                                                      RenderTargetUsage usage)
    {
        return std::make_unique<RenderTarget2D>(dev, w, h, false, SurfaceFormat::Color, depth,
                                                samples, usage);
    }

    // ================================================================ Group A: the first read

    /** @brief A1 -- THE reproducer. One target, one render, exactly one GetData. */
    void LegA1()
    {
        auto& dev = getGraphicsDeviceProperty();
        step("A1: RenderTarget2D(16,16,MSAA=4,DepthFormat::None,DiscardContents)");
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        std::printf("[INFO] A1 requested MultiSampleCount=4, applied=%d\n",
                    rt->getMultiSampleCountProperty());
        std::fflush(stdout);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "A1 first read", kMsaaContentReadable);
    }

    /** @brief A2 -- the first/second differential. Both reads asserted; they must agree. */
    void LegA2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        const std::vector<Color> first  = ReadAndAssert(*rt, "A2 first read", kMsaaContentReadable);
        const std::vector<Color> second = ReadAndAssert(*rt, "A2 second read", kMsaaContentReadable);

        if (!kTargetReadbackSupported) return;   // both reads were refused, asserted above
        int differing = 0;
        for (std::size_t i = 0; i < first.size(); ++i)
            if (!Exact(first[i], second[i])) ++differing;
        if (differing != 0)
            std::printf("        %d/%zu texels differ between the first and the second read\n",
                        differing, first.size());
        check(differing == 0,
              "A2 the second read is byte-identical to the first -- no priming read exists");
    }

    /** @brief A3 -- a NON-multisampled target read in the same frame stays byte-exact. */
    void LegA3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto msaa  = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        auto plain = MakeTarget(dev, kRT, kRT, 0, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *msaa);
        RenderPattern(dev, *plain);
        ReadAndAssert(*msaa,  "A3 multisampled first read", kMsaaContentReadable);
        ReadAndAssert(*plain, "A3 non-multisampled control", true);
    }

    /** @brief A4 -- first read after Clear ONLY: the flat clear colour, never zero. */
    void LegA4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt, /*clear=*/true, /*geometry=*/false);

        const int count = kRT * kRT;
        std::vector<Color> buf = SentinelBuffer(count);
        step("A4: GetData after Clear only");
        if (!IssueRead([&] { rt->GetData(buf.data(), kGuard, count); }, buf, "A4")) return;

        int untouched = 0, wrong = 0, zeroed = 0;
        for (int i = 0; i < count; ++i)
        {
            const Color got = buf[static_cast<std::size_t>(kGuard + i)];
            if (Exact(got, kSentinel)) ++untouched;
            if (Exact(got, Color(0, 0, 0, 0))) ++zeroed;
            if (!Near(got, kBase)) ++wrong;
        }
        check(untouched == 0, "A4 every requested element was written");
        GuardsIntact(buf, count, "A4");
        if (kMsaaContentReadable)
        {
            if (wrong) std::printf("        %d/%d wrong, %d exactly (0,0,0,0)\n", wrong, count, zeroed);
            check(wrong == 0, "A4 a Clear-only multisampled target reads back as its clear colour");
        }
        else
        {
            boundary("A4 multisampled content is not asserted on this renderer -- measured " +
                     std::to_string(zeroed) + "/" + std::to_string(count) + " exactly (0,0,0,0)");
        }
    }

    /** @brief A5 -- the target was created in an EARLIER frame, not this one. */
    void LegA5()
    {
        // The device has already presented at least one frame by the time Draw() runs for the
        // second time; this leg's own target is built on the first visit and read on the second.
        auto& dev = getGraphicsDeviceProperty();
        if (!deferredTarget_)
        {
            step("A5: creating the target this frame, reading it the NEXT frame");
            deferredTarget_ = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None,
                                         RenderTargetUsage::DiscardContents);
            wantsSecondFrame_ = true;
            return;
        }
        RenderPattern(dev, *deferredTarget_);
        ReadAndAssert(*deferredTarget_, "A5 target created in an earlier frame", kMsaaContentReadable);
    }

    /** @brief A6 -- the first read of a SECOND target, after another target has already been read. */
    void LegA6()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto first  = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *first);
        ReadAndAssert(*first, "A6 first target", kMsaaContentReadable);

        auto second = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *second);
        ReadAndAssert(*second, "A6 first read of a SECOND target", kMsaaContentReadable);
    }

    /**
     * @brief A7 -- A -> backbuffer -> A, then read A.
     *
     * A same-frame rebind routes the second bind cycle onto a fresh ordered view (REMED-GFX-018/065)
     * and puts a backbuffer batch between the two, so this is the sequence most likely to make a
     * readback observe the WRONG bind cycle's state.
     *
     * THE TWO CYCLES MUST DIFFER. The first spelling painted the SAME pattern twice and passed
     * pre-fix -- the backbuffer batch in the middle switched framebuffers and therefore resolved
     * cycle ONE, whose image happened to be identical, so a completely unresolved cycle two read as
     * a success. Cycle one now paints a flat colour that appears nowhere in the pattern, so a stale
     * cycle-one resolve is a visible failure.
     */
    void LegA7()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::PreserveContents);
        step("A7: cycle one paints a flat decoy colour");
        RenderPattern(dev, *rt, /*clear=*/true, /*geometry=*/false);

        step("A7: a backbuffer batch between the two bind cycles");
        dev.Clear(Color(90, 10, 10, 255));
        DrawFlatQuad(dev, Color(10, 90, 10, 255), -0.5f, -0.5f, 0.5f, 0.5f);

        step("A7: cycle two paints the real asymmetric pattern");
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "A7 read after A -> backbuffer -> A", kMsaaContentReadable);
    }

    /** @brief A8 -- two multisampled targets read in alternating order. */
    void LegA8()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto a = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        auto b = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *a);
        RenderPattern(dev, *b);
        ReadAndAssert(*a, "A8 target A first read",  kMsaaContentReadable);
        ReadAndAssert(*b, "A8 target B first read",  kMsaaContentReadable);
        ReadAndAssert(*a, "A8 target A second read", kMsaaContentReadable);
        ReadAndAssert(*b, "A8 target B second read", kMsaaContentReadable);
    }

    /** @brief A9 -- five reads of the same target inside one public frame. */
    void LegA9()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        std::vector<Color> reference;
        int stable = 0;
        for (int i = 0; i < 5; ++i)
        {
            const std::vector<Color> got =
                ReadAndAssert(*rt, "A9 read " + std::to_string(i + 1), kMsaaContentReadable);
            if (i == 0) reference = got;
            else if (reference.size() == got.size())
            {
                bool same = true;
                for (std::size_t k = 0; k < got.size(); ++k)
                    if (!Exact(reference[k], got[k])) { same = false; break; }
                if (same) ++stable;
            }
        }
        if (!kTargetReadbackSupported)
        {
            boundary("A9 all five reads were refused deterministically, asserted above");
            return;
        }
        check(stable == 4, "A9 five reads in one public frame all agree");
    }

    /** @brief A10 -- six create/render/read/dispose cycles; every FIRST read must be exact. */
    void LegA10()
    {
        auto& dev = getGraphicsDeviceProperty();
        int good = 0;
        for (int i = 0; i < 6; ++i)
        {
            auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None,
                                 RenderTargetUsage::DiscardContents);
            RenderPattern(dev, *rt);
            const int count = kRT * kRT;
            std::vector<Color> buf = SentinelBuffer(count);
            if (!IssueRead([&] { rt->GetData(buf.data(), kGuard, count); }, buf,
                           "A10 cycle " + std::to_string(i + 1)))
                continue;
            int untouched = 0, zeroed = 0;
            std::string mm;
            const bool matched = WindowMatches(buf, 0, 0, kRT, kRT, kRT, kRT, untouched, zeroed, mm);
            if (untouched == 0 && (matched || !kMsaaContentReadable)) ++good;
            rt->Dispose();
        }
        if (!kTargetReadbackSupported)
        {
            boundary("A10 six cycles each refused their read deterministically, asserted above");
            return;
        }
        if (!kMsaaContentReadable)
            boundary("A10 content unasserted on this renderer");
        check(good == 6, "A10 six create/render/first-read/dispose cycles all correct (" +
                             std::to_string(good) + "/6)");
    }

    // ================================================================ Group B: sample counts

    void SampleCountLeg(const char* id, int samples)
    {
        auto& dev = getGraphicsDeviceProperty();
        step(std::string(id) + ": MultiSampleCount=" + std::to_string(samples));
        auto rt = MakeTarget(dev, kRT, kRT, samples, DepthFormat::None,
                             RenderTargetUsage::DiscardContents);
        std::printf("[INFO] %s requested MultiSampleCount=%d, applied=%d\n", id, samples,
                    rt->getMultiSampleCountProperty());
        std::fflush(stdout);
        RenderPattern(dev, *rt);
        const bool assertContent = kMsaaContentReadable || samples == 0;
        ReadAndAssert(*rt, std::string(id) + " MSAA=" + std::to_string(samples) + " first read",
                      assertContent);
    }

    void LegB0() { SampleCountLeg("B0", 0); }
    void LegB1() { SampleCountLeg("B1", 1); }
    void LegB2() { SampleCountLeg("B2", 2); }
    void LegB4() { SampleCountLeg("B4", 4); }
    void LegB8() { SampleCountLeg("B8", 8); }
    void LegB16() { SampleCountLeg("B16", 16); }

    // ================================================================ Group C: depth formats

    void DepthLeg(const char* id, DepthFormat depth, const char* name)
    {
        auto& dev = getGraphicsDeviceProperty();
        step(std::string(id) + ": MSAA=4 with DepthFormat::" + name);
        auto rt = MakeTarget(dev, kRT, kRT, 4, depth, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, std::string(id) + " MSAA=4 + " + name, kMsaaContentReadable);
    }

    void LegC1() { DepthLeg("C1", DepthFormat::None, "None"); }
    void LegC2() { DepthLeg("C2", DepthFormat::Depth16, "Depth16"); }
    void LegC3() { DepthLeg("C3", DepthFormat::Depth24, "Depth24"); }
    void LegC4() { DepthLeg("C4", DepthFormat::Depth24Stencil8, "Depth24Stencil8"); }

    // ================================================================ Group D: usage

    void UsageLeg(const char* id, RenderTargetUsage usage, const char* name)
    {
        auto& dev = getGraphicsDeviceProperty();
        step(std::string(id) + ": MSAA=4 with RenderTargetUsage::" + name);
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, usage);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, std::string(id) + " MSAA=4 + " + name, kMsaaContentReadable);
    }

    void LegD1() { UsageLeg("D1", RenderTargetUsage::DiscardContents, "DiscardContents"); }
    void LegD2() { UsageLeg("D2", RenderTargetUsage::PreserveContents, "PreserveContents"); }
    void LegD3() { UsageLeg("D3", RenderTargetUsage::PlatformContents, "PlatformContents"); }

    // ================================================================ Group E: readback shapes

    /** @brief E1 -- the two-argument overload: `GetData(data, elementCount)`. */
    void LegE1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);

        const int count = kRT * kRT;
        std::vector<Color> buf(static_cast<std::size_t>(count) + kGuard, kSentinel);
        step("E1: GetData(dst, elementCount)");
        if (!IssueRead([&] { rt->GetData(buf.data(), count); }, buf, "E1")) return;
        int untouched = 0;
        for (int i = 0; i < count; ++i)
            if (Exact(buf[static_cast<std::size_t>(i)], kSentinel)) ++untouched;
        bool suffixOk = true;
        for (int i = 0; i < kGuard; ++i)
            if (!Exact(buf[static_cast<std::size_t>(count + i)], kSentinel)) suffixOk = false;
        check(untouched == 0, "E1 the 2-argument overload wrote every element");
        check(suffixOk, "E1 the 2-argument overload wrote nothing past its window");
    }

    /** @brief E2 -- an explicit FULL rectangle through the 5-argument overload. */
    void LegE2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        RectangleLeg(*rt, "E2 explicit full rectangle", Rectangle(0, 0, kRT, kRT), kRT, kRT);
    }

    /** @brief E3 -- a sub-rectangle covering exactly the mid-tone quadrant. */
    void LegE3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        RectangleLeg(*rt, "E3 sub-rectangle", Rectangle(0, kRT / 2, kRT / 2, kRT / 2), kRT, kRT);
    }

    /** @brief E4 -- a single row, and the FINAL row, of the same target. */
    void LegE4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        RectangleLeg(*rt, "E4 one row (y=4)", Rectangle(0, 4, kRT, 1), kRT, kRT);
        RectangleLeg(*rt, "E4 final row", Rectangle(0, kRT - 1, kRT, 1), kRT, kRT);
    }

    /** @brief E5 -- odd, non-power-of-two dimensions. */
    void LegE5()
    {
        auto& dev = getGraphicsDeviceProperty();
        step("E5: RenderTarget2D(13,7,MSAA=4)");
        auto rt = MakeTarget(dev, 13, 7, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "E5 odd 13x7 first read", kMsaaContentReadable);
    }

    /** @brief E6 -- a destination one element too small must be REFUSED, not truncated. */
    void LegE6()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);

        const int count = kRT * kRT;
        std::vector<Color> buf(static_cast<std::size_t>(count), kSentinel);
        bool threw = false;
        step("E6: GetData with elementCount one short of the rectangle");
        try
        {
            rt->GetData(0, nullptr, buf.data(), 0, count - 1);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(threw, "E6 a one-element-short destination is refused");

        bool intact = true;
        for (const Color& c : buf)
            if (!Exact(c, kSentinel)) { intact = false; break; }
        check(intact, "E6 the refused read wrote nothing at all");
    }

    /**
     * @brief Reads @p rect with a guarded destination and asserts the window against the pattern.
     */
    void RectangleLeg(RenderTarget2D& rt, const std::string& label, const Rectangle& rect,
                      int tw, int th)
    {
        const int count = rect.Width * rect.Height;
        std::vector<Color> buf = SentinelBuffer(count);
        step(label + ": GetData(0, rect(" + std::to_string(rect.X) + "," + std::to_string(rect.Y) +
             "," + std::to_string(rect.Width) + "," + std::to_string(rect.Height) + "), dst, " +
             std::to_string(kGuard) + ", " + std::to_string(count) + ")");
        if (!IssueRead([&] { rt.GetData(0, &rect, buf.data(), kGuard, count); }, buf, label))
            return;

        int untouched = 0, zeroed = 0;
        std::string mm;
        const bool matched = WindowMatches(buf, rect.X, rect.Y, rect.Width, rect.Height, tw, th,
                                           untouched, zeroed, mm);
        check(untouched == 0, label + ": every one of the " + std::to_string(count) +
                                  " requested elements was written");
        GuardsIntact(buf, count, label);
        if (kMsaaContentReadable)
        {
            if (!matched) std::printf("        first mismatch %s (%d/%d exactly zero)\n",
                                      mm.c_str(), zeroed, count);
            check(matched, label + ": the window holds that rectangle's resolved texels");
        }
        else
        {
            boundary(label + ": content unasserted, " + std::to_string(zeroed) +
                     "/" + std::to_string(count) + " exactly (0,0,0,0)");
        }
    }

    // ================================================================ Group F: lifetime

    /** @brief F1 -- dispose the target immediately after its first successful read. */
    void LegF1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "F1 read before dispose", kMsaaContentReadable);
        step("F1: Dispose immediately after the read returned");
        rt->Dispose();
        check(true, "F1 the target disposed cleanly straight after its first read");
    }

    /** @brief F2 -- the target outlives the frame and is torn down with the device. */
    void LegF2()
    {
        auto& dev = getGraphicsDeviceProperty();
        heldToTeardown_ = MakeTarget(dev, kRT, kRT, 4, DepthFormat::None,
                                     RenderTargetUsage::PreserveContents);
        RenderPattern(dev, *heldToTeardown_);
        ReadAndAssert(*heldToTeardown_, "F2 read, then held to device teardown", kMsaaContentReadable);
        check(true, "F2 the target is still alive when the frame ends");
    }

    /** @brief F3 -- targets of DIFFERENT sizes read in turn: no readback resource may be reused stale. */
    void LegF3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto small = MakeTarget(dev, 8, 8, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        auto big   = MakeTarget(dev, 32, 24, 4, DepthFormat::None, RenderTargetUsage::DiscardContents);
        RenderPattern(dev, *small);
        RenderPattern(dev, *big);
        ReadAndAssert(*small, "F3 8x8 first read",   kMsaaContentReadable);
        ReadAndAssert(*big,   "F3 32x24 first read", kMsaaContentReadable);
        ReadAndAssert(*small, "F3 8x8 re-read after a larger read", kMsaaContentReadable);
    }

    // ================================================================ Group G: regressions

    /** @brief G1 -- a mipmapped multisampled target: level 0 is the subject, level 1 is classified. */
    void LegG1()
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!kMipMappedTargetSupported)
        {
            boundary("G1 mip-mapped render targets are a declared boundary here (WEBGPU-53/54)");
            check(true, "G1 declared");
            return;
        }
        step("G1: RenderTarget2D(16,16,mipMap=true,MSAA=4)");
        std::unique_ptr<RenderTarget2D> rt;
        try
        {
            rt = std::make_unique<RenderTarget2D>(dev, kRT, kRT, true, SurfaceFormat::Color,
                                                  DepthFormat::None, 4,
                                                  RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            boundary(std::string("G1 a mipmapped multisampled target is refused here: ") + e.what());
            check(true, "G1 the refusal is a catchable public exception, not an abort");
            return;
        }
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "G1 mipmapped MSAA level 0 first read", kMsaaContentReadable);

        // Level 1 of a multisampled render target is a SEPARATE, independently tracked route (mip
        // regeneration, not resolve). Classified here so the boundary is on record, never fixed
        // here and never absorbed into this ticket.
        if (!kMsaaMipLevelProbeSafe)
        {
            boundary("G1 mip level 1 of a mipmapped multisampled target SEGFAULTS on this renderer "
                     "(REMED-GFX-186, isolated by this task's controls) -- a SIGSEGV cannot be "
                     "caught, so the probe is declared instead of issued. Level 0 above is exact.");
            check(true, "G1 mip level 1 behaviour is classified rather than absorbed");
            return;
        }
        const int lvl1 = (kRT / 2) * (kRT / 2);
        std::vector<Color> buf = SentinelBuffer(lvl1);
        const Rectangle r(0, 0, kRT / 2, kRT / 2);
        bool refused = false;
        try
        {
            rt->GetData(1, &r, buf.data(), kGuard, lvl1);
        }
        catch (const std::exception& e)
        {
            refused = true;
            boundary(std::string("G1 mip level 1 of a multisampled target is refused: ") + e.what());
        }
        if (!refused)
        {
            int zeroed = 0;
            for (int i = 0; i < lvl1; ++i)
                if (Exact(buf[static_cast<std::size_t>(kGuard + i)], Color(0, 0, 0, 0))) ++zeroed;
            boundary("G1 mip level 1 returned " + std::to_string(zeroed) + "/" +
                     std::to_string(lvl1) + " exactly (0,0,0,0) -- classified, NOT this ticket");
        }
        check(true, "G1 mip level 1 behaviour is classified rather than absorbed");
    }

    /** @brief G2 -- a plain non-multisampled target's readback is completely unaffected. */
    void LegG2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, 0, DepthFormat::Depth24, RenderTargetUsage::PreserveContents);
        RenderPattern(dev, *rt);
        ReadAndAssert(*rt, "G2 non-multisampled + depth first read", true);
        ReadAndAssert(*rt, "G2 non-multisampled + depth second read", true);
    }

    // ---------------------------------------------------------------- plumbing

    std::unique_ptr<RenderTarget2D> deferredTarget_;
    bool wantsSecondFrame_ = false;

    using Leg = void (RenderTargetMsaaFirstReadbackTest::*)();

    void runLeg(const char* id, Leg leg)
    {
        if (!wants(id)) return;
        (this->*leg)();
    }

    void Draw(const GameTime&) override
    {
        if (!kRasterizes)
        {
            boundary("HEADLESS does not rasterize; only the transfer contract is meaningful here");
        }

        runLeg("A1", &RenderTargetMsaaFirstReadbackTest::LegA1);
        runLeg("A2", &RenderTargetMsaaFirstReadbackTest::LegA2);
        runLeg("A3", &RenderTargetMsaaFirstReadbackTest::LegA3);
        runLeg("A4", &RenderTargetMsaaFirstReadbackTest::LegA4);
        runLeg("A5", &RenderTargetMsaaFirstReadbackTest::LegA5);
        if (wantsSecondFrame_) { wantsSecondFrame_ = false; return; }
        runLeg("A6", &RenderTargetMsaaFirstReadbackTest::LegA6);
        runLeg("A7", &RenderTargetMsaaFirstReadbackTest::LegA7);
        runLeg("A8", &RenderTargetMsaaFirstReadbackTest::LegA8);
        runLeg("A9", &RenderTargetMsaaFirstReadbackTest::LegA9);
        runLeg("A10", &RenderTargetMsaaFirstReadbackTest::LegA10);

        runLeg("B0", &RenderTargetMsaaFirstReadbackTest::LegB0);
        runLeg("B1", &RenderTargetMsaaFirstReadbackTest::LegB1);
        runLeg("B2", &RenderTargetMsaaFirstReadbackTest::LegB2);
        runLeg("B4", &RenderTargetMsaaFirstReadbackTest::LegB4);
        runLeg("B8", &RenderTargetMsaaFirstReadbackTest::LegB8);
        runLeg("B16", &RenderTargetMsaaFirstReadbackTest::LegB16);

        runLeg("C1", &RenderTargetMsaaFirstReadbackTest::LegC1);
        runLeg("C2", &RenderTargetMsaaFirstReadbackTest::LegC2);
        runLeg("C3", &RenderTargetMsaaFirstReadbackTest::LegC3);
        runLeg("C4", &RenderTargetMsaaFirstReadbackTest::LegC4);

        runLeg("D1", &RenderTargetMsaaFirstReadbackTest::LegD1);
        runLeg("D2", &RenderTargetMsaaFirstReadbackTest::LegD2);
        runLeg("D3", &RenderTargetMsaaFirstReadbackTest::LegD3);

        runLeg("E1", &RenderTargetMsaaFirstReadbackTest::LegE1);
        runLeg("E2", &RenderTargetMsaaFirstReadbackTest::LegE2);
        runLeg("E3", &RenderTargetMsaaFirstReadbackTest::LegE3);
        runLeg("E4", &RenderTargetMsaaFirstReadbackTest::LegE4);
        runLeg("E5", &RenderTargetMsaaFirstReadbackTest::LegE5);
        runLeg("E6", &RenderTargetMsaaFirstReadbackTest::LegE6);

        runLeg("F1", &RenderTargetMsaaFirstReadbackTest::LegF1);
        runLeg("F2", &RenderTargetMsaaFirstReadbackTest::LegF2);
        runLeg("F3", &RenderTargetMsaaFirstReadbackTest::LegF3);

        runLeg("G1", &RenderTargetMsaaFirstReadbackTest::LegG1);
        runLeg("G2", &RenderTargetMsaaFirstReadbackTest::LegG2);

        Finish();
    }

    void Finish()
    {
        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

public:
    explicit RenderTargetMsaaFirstReadbackTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /// Every leg, in the order the supervisor runs them. NONE of them may abort.
    const char* const kLegs[] = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10",
        "B0", "B1", "B2", "B4", "B8", "B16",
        "C1", "C2", "C3", "C4",
        "D1", "D2", "D3",
        "E1", "E2", "E3", "E4", "E5", "E6",
        "F1", "F2", "F3",
        "G1", "G2",
    };

#if CNA_GFX154_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever.
    constexpr unsigned kLegTimeoutSeconds = 180;

    /**
     * @brief Runs one leg in a child process and classifies the outcome exactly.
     *
     * Process isolation is not only crash containment here: "the FIRST readback of the process" is
     * a property this file asserts, and only a fresh process can guarantee it.
     */
    bool RunLegIsolated(const char* exePath, const char* legId, bool& skipped)
    {
        skipped = false;
        const std::string arg = std::string("--leg=") + legId;

        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] supervisor: fork() failed for leg %s\n", legId);
            std::fflush(stdout);
            return false;
        }
        if (pid == 0)
        {
            alarm(kLegTimeoutSeconds);
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
            const char* core = WCOREDUMP(status) ? " (core dumped)" : "";
            if (sig == SIGALRM)
                std::printf("[TIMEOUT] leg %s: no result within %u s (hang, or a readback that "
                            "never completed)\n", legId, kLegTimeoutSeconds);
            else if (sig == SIGTRAP)
                std::printf("[FATAL] leg %s: SIGTRAP%s -- a native ASSERT killed the process; the "
                            "last [STEP] line names the call\n", legId, core);
            else if (sig == SIGABRT)
                std::printf("[FATAL] leg %s: SIGABRT%s -- std::terminate or abort()\n", legId, core);
            else if (sig == SIGSEGV)
                std::printf("[FATAL] leg %s: SIGSEGV%s\n", legId, core);
            else
                std::printf("[FATAL] leg %s: killed by signal %d (%s)%s\n", legId, sig,
                            strsignal(sig), core);
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == CNA::Examples::kSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: no usable display\n", legId);
                std::fflush(stdout);
                return true;
            }
            if (code == 0) return true;
            if (code == 127)
                std::printf("[FAIL] leg %s: execv failed -- the child never started\n", legId);
            else
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

#if CNA_GFX154_CAN_FORK
    if (onlyLeg.empty())
    {
        const int total = static_cast<int>(sizeof(kLegs) / sizeof(kLegs[0]));
        std::printf("[INFO] REMED-GFX-154 supervisor on %s: %d legs, each in its own process\n",
                    kRendererName, total);
        std::fflush(stdout);
        int passed = 0, skippedCount = 0;
        for (const char* legId : kLegs)
        {
            bool skipped = false;
            if (RunLegIsolated(argv[0], legId, skipped)) ++passed;
            if (skipped) ++skippedCount;
        }
        std::printf("[INFO] supervisor: %d/%d legs passed (%d skipped)\n", passed, total,
                    skippedCount);
        std::fflush(stdout);
        if (skippedCount == total) return CNA::Examples::kSkipExitCode;
        return (passed == total) ? 0 : 1;
    }
#endif

    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RenderTargetMsaaFirstReadbackTest game(std::move(onlyLeg));
    game.Run();
    return game.Result();
}
