// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-189 / REMED-GFX-192 -- a `RenderTarget2D` mip level OUTSIDE the declared chain must be
// REFUSED deterministically in the shared Texture2D layer, before any dimension calculation or
// native call, with the caller's destination left untouched.
//
// THE DEFECT
// ----------
// On VULKAN the public sequence
//
//     RenderTarget2D rt(dev, 16, 16, /*mipMap=*/true, Color, DepthFormat::None, 4, DiscardContents);
//     ... render ...
//     const int n = rt.getLevelCountProperty();          // 5
//     Rectangle one(0, 0, 1, 1);
//     rt.GetData(n, &one, dst, guard, 1);                // <-- returns NORMALLY and WRITES dst
//
// did not throw. `VulkanRenderTargetRenderer::GetData` range-checked nothing, so the request reached
// `vkCmdCopyImageToBuffer` with `imageSubresource.mipLevel = 5` on a five-level image, the copy
// executed as undefined behaviour, and the caller was handed a texel that LOOKS like content. That
// is precisely the invented result REMED-GFX-127 and REMED-GFX-130 exist to forbid: a caller cannot
// tell it apart from a successful read.
//
// Every sibling route on this very renderer already had the guard -- `VulkanTexture3DRenderer`,
// `VulkanTextureCubeRenderer` and `VulkanRenderTargetCubeRenderer` all test
// `level < 0 || level >= levelCount_` -- and `VulkanRenderTargetRenderer` was the one route that
// never learned it. That is the same shape REMED-GFX-186 closed on SDL_GPU, where the symptom was a
// SIGSEGV instead of fabrication.
//
// THE PUBLIC CONTRACT THIS FILE ENCODES
// -------------------------------------
// For every resource exposing mip-level readback:
//
//   * the valid levels are exactly [0, LevelCount);
//   * `level < 0` is invalid, and `level >= LevelCount` is invalid;
//   * an invalid level is a CALLER ERROR -- not a missing capability -- rejected deterministically
//     before any native execution;
//   * the destination storage stays COMPLETELY unchanged: prefix, requested window and suffix;
//   * no native image view, copy, transition, command buffer, submit or wait is created for it;
//   * for every VALID level the dimensions come from the public dimensions and that level, the
//     rectangle is checked against THAT level's dimensions, the destination window follows
//     REMED-GFX-149, and the complete requested range is written synchronously.
//
// REMED-GFX-192 puts both ends of the range through one shared `Texture2D::GetData` validation path,
// before the old `base >> level` dimension calculation. Both ends are asserted here so that path
// cannot drift back into renderer-specific handling.
//
// THE ORACLE
// ----------
// Two independent oracles, because "it returned something" was exactly the failure mode:
//
//   1. REFUSAL -- a sentinel-filled destination with a protected prefix and suffix. An invalid
//      request must throw a catchable public exception AND leave every single element holding the
//      sentinel. A refusal that quietly wrote zeroes, or that clamped to the last valid level,
//      fails this just as loudly as the fabrication did.
//
//   2. LEVEL IDENTITY -- the level-0 image is four flat quadrants whose colours differ by >= 90 on
//      some channel. Because every quadrant boundary sits on an even texel index, a correctly
//      generated level L is that SAME four-quadrant image at that level's own size. Critically, the
//      FINAL 1x1 level of a chain is a blend of all four quadrants and therefore differs from EVERY
//      single quadrant colour -- which is what makes leg C1 able to name the level a pre-fix
//      fabricated read really came from: texel (0,0) of level 0 is the top-left quadrant colour,
//      while texel (0,0) of the final level is not. "Clamped to the last level" and "read level 0"
//      are therefore distinguishable answers, not one undifferentiated "fabricated".
//
// PROCESS ISOLATION
// -----------------
// Each leg runs in its own forked child. The Vulkan symptom is not a signal, but the sibling defect
// this file generalises (REMED-GFX-186) was, a control renderer may still abort, and "the FIRST
// readback of the process" is a property leg G1 asserts -- which only a fresh process can give.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
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

#if defined(CNA_RENDERER_SOFTWARE)
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#endif

#include <algorithm>
#include <climits>
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
#define CNA_GFX189_CAN_FORK 1
#else
#define CNA_GFX189_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 64;   ///< Backbuffer width. Nothing in this file reads the backbuffer.
    constexpr int kBBH = 64;   ///< Backbuffer height.

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
#else
#error "REMED-GFX-189: this renderer has no declared invalid-mip-level contract."
#endif

    /**
     * @brief Whether this renderer can read a render target's colour attachment back at all.
     *
     * False on HEADLESS, which rasterizes nothing and owns no colour to return. Its refusal is
     * REMED-GFX-127/130's deterministic public one, and it is ASSERTED here rather than skipped.
     * Note that this changes nothing about the INVALID-level legs: a refusal is the required answer
     * there either way, so those legs assert exactly the same contract on every renderer.
     */
    constexpr bool kTargetReadbackSupported =
#if defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    /**
     * @brief Whether a render target may be built with a mip chain here.
     *
     * False on WEBGPU, whose `RenderTarget2D` constructor raises a catchable `std::runtime_error`
     * for `mipMap=true` (WEBGPU-53/54) -- a separately tracked boundary, not this task's subject.
     */
    constexpr bool kMipMappedTargetSupported =
#if defined(CNA_RENDERER_WEBGPU)
        false;
#else
        true;
#endif

    /**
     * @brief Whether this renderer POPULATES a render target's levels above zero at all.
     *
     * False on SOFTWARE, which stores level 0 only and raises a specific catchable refusal for any
     * other level. That is that renderer's own declared boundary; it is asserted, never skipped.
     */
    constexpr bool kTargetMipReadbackSupported =
#if defined(CNA_RENDERER_SOFTWARE)
        false;
#else
        true;
#endif

    /**
     * @brief Whether `SetRenderTargets` with more than one attachment is executed here.
     *
     * False on SOFTWARE and HEADLESS. Leg H1 declares the refusal rather than skipping, so an MRT
     * route that silently started working would be visible.
     */
    constexpr bool kMrtSupported =
#if defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    // ---- the asymmetric pattern -------------------------------------------------------------
    //
    // Identical to REMED-GFX-186's, deliberately: the two files then agree texel for texel on what a
    // correctly generated level contains, so a disagreement between them is a real difference and
    // never a difference of oracle. Every channel of every colour is nonzero, so a zeroed channel is
    // a visible failure rather than something a tolerance could absorb.
    const Color kBase(20, 60, 140, 255);    ///< Clear colour; also the bottom-right quadrant.
    const Color kTL  (250, 25, 15, 255);    ///< Top-left quadrant.
    const Color kTR  (15, 245, 30, 255);    ///< Top-right quadrant.
    const Color kBL  (120, 130, 125, 255);  ///< Bottom-left quadrant -- the mid-tone.

    /// Absent from the rendered image on every channel, and distinct from (0,0,0,0).
    const Color kSentinel(7, 3, 11, 199);

    /// Vertex-colour round trips differ by a unit or two across renderers, MSAA resolves and blits.
    constexpr int kTol = 14;

    /// Protected elements written before and after every requested destination window.
    constexpr int kGuard = 5;

    /// The dimension of mip @p level of a @p base-sized axis -- XNA's own `max(1, base >> level)`.
    int LevelDim(int base, int level)
    {
        return std::max(1, base >> level);
    }

    /// The public `LevelCount` a `mipMap=true` target of these dimensions must report.
    int ChainLength(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

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

    /** @brief The colour the asymmetric pattern puts at (@p x, @p y) of a @p w x @p h image. */
    Color Expected(int x, int y, int w, int h)
    {
        const bool left = x < w / 2;
        const bool top  = y < h / 2;
        if (top && left)  return kTL;
        if (top && !left) return kTR;
        if (!top && left) return kBL;
        return kBase;
    }

    /** @brief True when (@p x, @p y) of the LEVEL-0 image is far enough from a quadrant SEAM. */
    bool Interior(int x, int y, int w, int h)
    {
        const int dx = x < w / 2 ? (w / 2 - 1 - x) : (x - w / 2);
        const int dy = y < h / 2 ? (h / 2 - 1 - y) : (y - h / 2);
        return dx >= 2 && dy >= 2;
    }

    /** @brief The exact expected colour of level-@p level texel (@p x, @p y), or false if it blends. */
    bool ExactAtLevel(int x, int y, int level, int w0, int h0, Color& out)
    {
        const int span = 1 << level;
        const int x0 = x * span;
        const int y0 = y * span;
        if (x0 + span > w0 || y0 + span > h0) return false;
        Color first = Expected(x0, y0, w0, h0);
        for (int fy = y0; fy < y0 + span; ++fy)
            for (int fx = x0; fx < x0 + span; ++fx)
            {
                if (!Interior(fx, fy, w0, h0)) return false;
                if (!Exact(Expected(fx, fy, w0, h0), first)) return false;
            }
        out = first;
        return true;
    }

    /** @brief The mathematically bounded channel envelope every legitimate generated texel lies in. */
    bool WithinPaletteEnvelope(const Color& c)
    {
        auto lo = [](int a, int b, int cc, int d) { return std::min(std::min(a, b), std::min(cc, d)); };
        auto hi = [](int a, int b, int cc, int d) { return std::max(std::max(a, b), std::max(cc, d)); };
        const int rLo = lo(kBase.getRProperty(), kTL.getRProperty(), kTR.getRProperty(), kBL.getRProperty());
        const int rHi = hi(kBase.getRProperty(), kTL.getRProperty(), kTR.getRProperty(), kBL.getRProperty());
        const int gLo = lo(kBase.getGProperty(), kTL.getGProperty(), kTR.getGProperty(), kBL.getGProperty());
        const int gHi = hi(kBase.getGProperty(), kTL.getGProperty(), kTR.getGProperty(), kBL.getGProperty());
        const int bLo = lo(kBase.getBProperty(), kTL.getBProperty(), kTR.getBProperty(), kBL.getBProperty());
        const int bHi = hi(kBase.getBProperty(), kTL.getBProperty(), kTR.getBProperty(), kBL.getBProperty());
        const int r = c.getRProperty(), g = c.getGProperty(), b = c.getBProperty(), a = c.getAProperty();
        return r >= rLo - kTol && r <= rHi + kTol
            && g >= gLo - kTol && g <= gHi + kTol
            && b >= bLo - kTol && b <= bHi + kTol
            && a >= 255 - kTol;
    }
}

/**
 * @brief REMED-GFX-189's contract: a mip level outside the declared chain must be refused.
 */
class RenderTargetInvalidMipLevelTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Leg G5's target: an invalid read is issued, then it is handed to device teardown alive.
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

    void note(const std::string& text) const
    {
        std::printf("[INFO] %s\n", text.c_str());
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
     * test, with nothing between them. No `Present`, no extra frame, no dummy bind, no sleep.
     */
    void RenderPattern(GraphicsDevice& dev, RenderTarget2D& rt)
    {
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        dev.SetRenderTarget(&rt);
        dev.Clear(kBase);
        DrawFlatQuad(dev, kTL, -1.f,  0.f,  0.f,  1.f);
        DrawFlatQuad(dev, kTR,  0.f,  0.f,  1.f,  1.f);
        DrawFlatQuad(dev, kBL, -1.f, -1.f,  0.f,  0.f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    // ---------------------------------------------------------------- the sentinel oracle

    /** @brief A destination of `kGuard + count + kGuard` elements, every one the sentinel colour. */
    static std::vector<Color> SentinelBuffer(int count)
    {
        return std::vector<Color>(static_cast<std::size_t>(count) + 2 * kGuard, kSentinel);
    }

    /** @brief Asserts the protected prefix and suffix around a `count`-element window are untouched. */
    bool GuardsIntact(const std::vector<Color>& buf, int count, const std::string& label)
    {
        bool ok = true;
        for (int i = 0; i < kGuard && ok; ++i)
            if (!Exact(buf[static_cast<std::size_t>(i)], kSentinel))
            {
                std::printf("        prefix element %d = %s, expected sentinel %s\n", i,
                            ColorText(buf[static_cast<std::size_t>(i)]).c_str(),
                            ColorText(kSentinel).c_str());
                ok = false;
            }
        for (int i = 0; i < kGuard && ok; ++i)
        {
            const std::size_t idx = static_cast<std::size_t>(kGuard + count + i);
            if (!Exact(buf[idx], kSentinel))
            {
                std::printf("        suffix element %d = %s, expected sentinel %s\n", i,
                            ColorText(buf[idx]).c_str(), ColorText(kSentinel).c_str());
                ok = false;
            }
        }
        check(ok, label + ": protected prefix and suffix are untouched");
        return ok;
    }

    /**
     * @brief THE central assertion: an invalid level is refused, and writes absolutely nothing.
     *
     * One public `GetData`, into a sentinel-filled destination with a protected prefix and suffix.
     * The contract has three independent parts, each of which the pre-fix Vulkan behaviour broke or
     * would have broken:
     *
     *   * it must THROW -- not return normally, and not die by a signal;
     *   * the exception must be catchable, so a game can degrade gracefully;
     *   * EVERY element of the destination -- prefix, requested window and suffix alike -- must
     *     still hold the sentinel. Clamping to the last valid level, returning level 0, zero-filling
     *     and handing back staging memory all fail here identically, which is the point: the check
     *     does not have to know WHICH wrong answer a renderer would have invented.
     *
     * @param elementCount how many elements the request claims; the window is that wide.
     * @param startIndex   the destination offset, so REMED-GFX-149's window is exercised too.
     */
    void ExpectLevelRejected(RenderTarget2D& rt, int level, const Rectangle* rect,
                             int elementCount, int startIndex, const std::string& label)
    {
        std::vector<Color> buf = SentinelBuffer(std::max(elementCount, 1) + startIndex);
#if defined(CNA_RENDERER_SOFTWARE)
        auto* softwareRenderer =
            dynamic_cast<CNA::Internal::Renderers::Software::SoftwareRenderTargetRenderer*>(
                rt.GetRenderTargetRenderer());
        const std::size_t softwareCallsBefore = softwareRenderer != nullptr
            ? softwareRenderer->GetReadbackCallCountEXT() : 0;
#endif
        step(label + ": GetData(level=" + std::to_string(level) + ", startIndex=" +
             std::to_string(startIndex) + ", elementCount=" + std::to_string(elementCount) +
             ") must be REFUSED before any native call");

        bool threw = false, wasOutOfRange = false;
        std::string what;
        try
        {
            rt.GetData(level, rect, buf.data(), startIndex, elementCount);
        }
        catch (const std::out_of_range& e) { threw = true; wasOutOfRange = true; what = e.what(); }
        catch (const std::exception& e)    { threw = true; what = e.what(); }

        check(threw, label + ": rejected through a catchable public exception, not a signal and not "
                             "a normal return");
        if (threw) note(label + ": " + std::string(wasOutOfRange ? "std::out_of_range -- " : "") + what);

        bool untouched = true;
        std::size_t firstTouched = 0;
        for (std::size_t i = 0; i < buf.size(); ++i)
            if (!Exact(buf[i], kSentinel)) { untouched = false; firstTouched = i; break; }
        if (!untouched)
            std::printf("        element %zu = %s -- the refused read WROTE the destination\n",
                        firstTouched, ColorText(buf[firstTouched]).c_str());
        check(untouched, label + ": the refused read left the WHOLE destination untouched -- no "
                                 "clamped level, no level 0, no zero fill, no staging memory");

        check(wasOutOfRange, label + ": the refusal is a std::out_of_range from the shared "
                                     "Texture2D mip-level validator");
        check(what.find("level") != std::string::npos,
              label + ": the exception identifies the level parameter");
        if (level >= rt.getLevelCountProperty())
        {
            check(what.find(std::to_string(level)) != std::string::npos &&
                      what.find(std::to_string(rt.getLevelCountProperty())) != std::string::npos,
                  label + ": the exception identifies requested level " + std::to_string(level) +
                      " and LevelCount " + std::to_string(rt.getLevelCountProperty()));
        }
#if defined(CNA_RENDERER_SOFTWARE)
        check(softwareRenderer != nullptr &&
                  softwareRenderer->GetReadbackCallCountEXT() == softwareCallsBefore,
              label + ": zero Software renderer readback calls");
#endif
    }

    /**
     * @brief Runs @p doRead, or asserts this renderer's deterministic readback refusal instead.
     * @return true when the read really happened and the buffer may be inspected.
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
     * @brief Reads one rectangle of one VALID mip level and asserts the whole contract for it.
     *
     * This is the other half of the ticket: proving the guard did not cost a single valid read. It
     * asserts the complete requested window was written, the guards survived, nothing is (0,0,0,0),
     * every texel is inside the palette envelope, and every texel whose level-0 footprint is a clean
     * single-quadrant block equals that quadrant's colour -- which a read of the WRONG level cannot
     * satisfy, since a level-0 read of the same rectangle is entirely one quadrant.
     *
     * @return the texels read, so two reads can be compared byte for byte.
     */
    std::vector<Color> ReadLevelRect(RenderTarget2D& rt, int level, int rx, int ry, int rw, int rh,
                                     int w0, int h0, const std::string& label,
                                     bool contentAsserted = true)
    {
        const int count = rw * rh;
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle r(rx, ry, rw, rh);

        if (level > 0 && !kTargetMipReadbackSupported)
        {
            step(label + ": GetData(level=" + std::to_string(level) + ") -- this renderer stores "
                 "level 0 only and must REFUSE");
            bool threw = false;
            std::string what;
            try { rt.GetData(level, &r, buf.data(), kGuard, count); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(threw, label + ": a level this renderer does not populate is refused through a "
                                 "catchable public exception, not a signal");
            if (threw) note(label + ": " + what);
            bool untouched = true;
            for (const Color& c : buf)
                if (!Exact(c, kSentinel)) { untouched = false; break; }
            check(untouched, label + ": the refused read left the whole destination untouched");
            return std::vector<Color>(static_cast<std::size_t>(count), kSentinel);
        }

        step(label + ": GetData(level=" + std::to_string(level) + ", rect=(" + std::to_string(rx) +
             "," + std::to_string(ry) + "," + std::to_string(rw) + "x" + std::to_string(rh) +
             "), startIndex=" + std::to_string(kGuard) + ", elementCount=" + std::to_string(count) + ")");
        if (!IssueRead([&] { rt.GetData(level, &r, buf.data(), kGuard, count); }, buf, label))
            return std::vector<Color>(static_cast<std::size_t>(count), kSentinel);

        int untouched = 0, zeroed = 0, outsideEnvelope = 0, exactChecked = 0, exactWrong = 0;
        std::string firstMismatch;
        const Color black(0, 0, 0, 0);

        for (int row = 0; row < rh; ++row)
            for (int col = 0; col < rw; ++col)
            {
                const Color got = buf[static_cast<std::size_t>(kGuard + row * rw + col)];
                if (Exact(got, kSentinel)) { ++untouched; continue; }
                if (Exact(got, black)) ++zeroed;
                if (!WithinPaletteEnvelope(got)) ++outsideEnvelope;

                Color want = kSentinel;
                if (ExactAtLevel(rx + col, ry + row, level, w0, h0, want))
                {
                    ++exactChecked;
                    if (!Near(got, want))
                    {
                        if (exactWrong == 0)
                            firstMismatch = "(" + std::to_string(rx + col) + "," +
                                            std::to_string(ry + row) + ") got " + ColorText(got) +
                                            " want " + ColorText(want);
                        ++exactWrong;
                    }
                }
            }

        check(untouched == 0,
              label + ": every one of the " + std::to_string(count) +
                  " requested elements was written (" + std::to_string(untouched) +
                  " still held the sentinel)");
        GuardsIntact(buf, count, label);

        if (!contentAsserted)
        {
            boundary(label + ": generated mip CONTENT is not asserted for this level here -- "
                             "measured " + std::to_string(zeroed) + "/" + std::to_string(count) +
                     " exactly (0,0,0,0), first texel " +
                     ColorText(buf[static_cast<std::size_t>(kGuard)]));
        }
        else
        {
            check(zeroed == 0,
                  label + ": no texel is exactly (0,0,0,0) (" + std::to_string(zeroed) +
                      "/" + std::to_string(count) + " were) -- every palette colour has all four "
                      "channels nonzero, so that value can only be fabricated");
            check(outsideEnvelope == 0,
                  label + ": every texel lies inside the palette's bounded channel envelope (" +
                      std::to_string(outsideEnvelope) + "/" + std::to_string(count) + " did not)");
            if (exactChecked > 0)
                check(exactWrong == 0,
                      label + ": all " + std::to_string(exactChecked) +
                          " texels whose level-0 footprint is a single clean quadrant hold that "
                          "quadrant's colour" +
                          (exactWrong ? (" -- first mismatch " + firstMismatch) : ""));
        }

        return std::vector<Color>(buf.begin() + kGuard, buf.begin() + kGuard + count);
    }

    /** @brief Reads the COMPLETE mip level @p level and asserts the contract for it. */
    std::vector<Color> ReadWholeLevel(RenderTarget2D& rt, int level, const std::string& label,
                                      bool contentAsserted = true)
    {
        const int w0 = rt.getWidthProperty();
        const int h0 = rt.getHeightProperty();
        return ReadLevelRect(rt, level, 0, 0, LevelDim(w0, level), LevelDim(h0, level), w0, h0,
                             label, contentAsserted);
    }

    /** @brief Builds a target, or declares this renderer's catchable refusal and returns null. */
    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev, int w, int h, bool mipMap,
                                               int samples, DepthFormat depth,
                                               RenderTargetUsage usage, const std::string& label)
    {
        step(label + ": RenderTarget2D(" + std::to_string(w) + "," + std::to_string(h) +
             ",mipMap=" + (mipMap ? "true" : "false") + ",MSAA=" + std::to_string(samples) + ")");
        try
        {
            return std::make_unique<RenderTarget2D>(dev, w, h, mipMap, SurfaceFormat::Color, depth,
                                                    samples, usage);
        }
        catch (const std::exception& e)
        {
            boundary(label + ": this construction is refused here: " + e.what());
            check(true, label + ": the refusal is a catchable public exception, not an abort");
            return nullptr;
        }
    }

    /** @brief Builds and renders a mipmapped target, honouring this renderer's declared boundaries. */
    std::unique_ptr<RenderTarget2D> MakeRenderedTarget(GraphicsDevice& dev, int w, int h,
                                                       bool mipMap, int samples,
                                                       const std::string& label)
    {
        if (mipMap && !kMipMappedTargetSupported)
        {
            auto refused = MakeTarget(dev, w, h, true, samples, DepthFormat::None,
                                      RenderTargetUsage::DiscardContents, label);
            return refused;
        }
        auto rt = MakeTarget(dev, w, h, mipMap, samples, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, label);
        if (rt) RenderPattern(dev, *rt);
        return rt;
    }

    /** @brief Prints and asserts the public level count of a freshly built target. */
    void AssertChain(RenderTarget2D& rt, bool mipMap, const std::string& label)
    {
        const int w = rt.getWidthProperty();
        const int h = rt.getHeightProperty();
        const int want = mipMap ? ChainLength(w, h) : 1;
        const int got = rt.getLevelCountProperty();
        note(label + ": " + std::to_string(w) + "x" + std::to_string(h) +
             " requestedSamples->applied=" + std::to_string(rt.getMultiSampleCountProperty()) +
             " LevelCount=" + std::to_string(got) + " (contract " + std::to_string(want) + ")");
        check(got == want,
              label + ": LevelCount is " + std::to_string(want) +
                  ", the declaration every valid level index is measured against");
    }

    /** @brief Reads every VALID level of @p rt, asserting each one. */
    void ReadEveryValidLevel(RenderTarget2D& rt, const std::string& label)
    {
        const int n = rt.getLevelCountProperty();
        for (int level = 0; level < n; ++level)
            ReadWholeLevel(rt, level, label + " level " + std::to_string(level));
    }

    /** @brief Exercises REMED-GFX-192's shared SetData and CPU mip-buffer path. */
    void ExercisePlainTextureSetDataContract(GraphicsDevice& dev)
    {
        constexpr int width = 13;
        constexpr int height = 7;
        constexpr int sourceStart = 3;
        constexpr int destinationStart = 4;
        step("A1 Texture2D: populate every valid mip, reject invalid SetData, read every mip back");
        Texture2D texture(dev, width, height, true, SurfaceFormat::Color);
        const int n = texture.getLevelCountProperty();
        check(n == ChainLength(width, height),
              "A1 Texture2D: LevelCount matches the complete 13x7 mip chain");

        std::vector<std::vector<Color>> expected(static_cast<std::size_t>(n));
        for (int level = 0; level < n; ++level)
        {
            const int levelW = LevelDim(width, level);
            const int levelH = LevelDim(height, level);
            const int count = levelW * levelH;
            std::vector<Color> source(static_cast<std::size_t>(sourceStart + count + 2), kSentinel);
            expected[static_cast<std::size_t>(level)].reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                const Color value(31 + level * 41 + i % 11,
                                  47 + level * 29 + i % 13,
                                  63 + level * 17 + i % 19,
                                  255);
                source[static_cast<std::size_t>(sourceStart + i)] = value;
                expected[static_cast<std::size_t>(level)].push_back(value);
            }
            const std::vector<Color> sourceBefore = source;
            bool threw = false;
            try { texture.SetData(level, nullptr, source.data(), sourceStart, count); }
            catch (const std::exception& e)
            {
                threw = true;
                note("A1 Texture2D valid SetData level " + std::to_string(level) + ": " + e.what());
            }
            check(!threw,
                  "A1 Texture2D: valid level " + std::to_string(level) + " SetData succeeds at " +
                      std::to_string(levelW) + "x" + std::to_string(levelH));
            bool sourceUntouched = source.size() == sourceBefore.size();
            for (std::size_t i = 0; sourceUntouched && i < source.size(); ++i)
                sourceUntouched = Exact(source[i], sourceBefore[i]);
            check(sourceUntouched,
                  "A1 Texture2D: valid SetData level " + std::to_string(level) +
                      " leaves its source unchanged");
        }

        const int invalidLevels[] = {-1, n, 1000, INT_MAX};
        for (int level : invalidLevels)
        {
            std::vector<Color> source(7, kSentinel);
            source[static_cast<std::size_t>(sourceStart)] = Color(201, 111, 77, 255);
            const std::vector<Color> sourceBefore = source;
            bool wasOutOfRange = false;
            std::string what;
            try { texture.SetData(level, nullptr, source.data(), sourceStart, 1); }
            catch (const std::out_of_range& e) { wasOutOfRange = true; what = e.what(); }
            catch (const std::exception& e) { what = e.what(); }
            check(wasOutOfRange,
                  "A1 Texture2D: SetData level " + std::to_string(level) +
                      " is rejected by the shared std::out_of_range path");
            if (!what.empty()) note("A1 Texture2D invalid SetData: " + what);
            bool sourceUntouched = source.size() == sourceBefore.size();
            for (std::size_t i = 0; sourceUntouched && i < source.size(); ++i)
                sourceUntouched = Exact(source[i], sourceBefore[i]);
            check(sourceUntouched,
                  "A1 Texture2D: rejected SetData level " + std::to_string(level) +
                      " leaves its source unchanged");

            std::vector<Color> rejectedDestination(9, kSentinel);
            const std::vector<Color> destinationBefore = rejectedDestination;
            bool getWasOutOfRange = false;
            std::string getWhat;
            try
            {
                texture.GetData(level, nullptr, rejectedDestination.data(), destinationStart, 1);
            }
            catch (const std::out_of_range& e)
            {
                getWasOutOfRange = true;
                getWhat = e.what();
            }
            catch (const std::exception& e) { getWhat = e.what(); }
            check(getWasOutOfRange && getWhat.find("level") != std::string::npos,
                  "A1 Texture2D: GetData level " + std::to_string(level) +
                      " is rejected as the level argument by the same shared path");
            check(rejectedDestination == destinationBefore,
                  "A1 Texture2D: rejected GetData level " + std::to_string(level) +
                      " leaves destination prefix, range and suffix unchanged");
        }

        for (int level = 0; level < n; ++level)
        {
            const int count = LevelDim(width, level) * LevelDim(height, level);
            std::vector<Color> destination(
                static_cast<std::size_t>(destinationStart + count + 2 + kGuard), kSentinel);
            bool threw = false;
            try
            {
                texture.GetData(level, nullptr, destination.data(), destinationStart, count + 2);
            }
            catch (const std::exception& e)
            {
                threw = true;
                note("A1 Texture2D valid GetData level " + std::to_string(level) + ": " + e.what());
            }
            bool exact = !threw;
            for (int i = 0; exact && i < count; ++i)
                exact = Exact(destination[static_cast<std::size_t>(destinationStart + i)],
                              expected[static_cast<std::size_t>(level)][static_cast<std::size_t>(i)]);
            check(exact,
                  "A1 Texture2D: valid level " + std::to_string(level) +
                      " retains its exact dimensions and contents after all rejected SetData calls");
            bool guardsUntouched = true;
            for (int i = 0; guardsUntouched && i < destinationStart; ++i)
                guardsUntouched = Exact(destination[static_cast<std::size_t>(i)], kSentinel);
            for (std::size_t i = static_cast<std::size_t>(destinationStart + count);
                 guardsUntouched && i < destination.size(); ++i)
                guardsUntouched = Exact(destination[i], kSentinel);
            check(guardsUntouched,
                  "A1 Texture2D: valid level " + std::to_string(level) +
                      " preserves the GFX-149 destination prefix and surplus capacity");
        }
    }

    // ================================================================ the legs

    /**
     * @brief A1 -- the canonical invalid level indices on a mipmapped NPOT target.
     *
     * 13x7 is deliberately small, asymmetric and non-power-of-two: its chain is 4 deep, both axes
     * reach 1 at different levels, and `13 >> 4` and `7 >> 4` are both 0, so the shared layer's
     * `max(1, base >> level)` clamp hands the renderer a plausible 1x1 rectangle for a level that
     * does not exist. That clamp is exactly why the pre-fix native copy looked well-formed.
     */
    void LegA1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 13, 7, true, 0, "A1");
        if (!rt) return;
        AssertChain(*rt, true, "A1");
        const int n = rt->getLevelCountProperty();

        ReadWholeLevel(*rt, 0, "A1 positive control level 0");

        const Rectangle one(0, 0, 1, 1);
        ExpectLevelRejected(*rt, n,       &one, 1, kGuard, "A1 level == LevelCount");
        ExpectLevelRejected(*rt, n + 1,   &one, 1, kGuard, "A1 level == LevelCount + 1");
        ExpectLevelRejected(*rt, INT_MAX, &one, 1, kGuard, "A1 level == INT_MAX");
        ExpectLevelRejected(*rt, -1,      &one, 1, kGuard, "A1 level == -1");

        // The upper and lower ends of the SAME range, asserted in one place so they cannot drift
        // apart again. REMED-GFX-192 routes both through the one shared guard before dimensions.
        ReadWholeLevel(*rt, n - 1, "A1 final valid level survives the four rejections");

        ExercisePlainTextureSetDataContract(dev);
    }

    /**
     * @brief A2 -- an invalid level combined with every rectangle and destination shape.
     *
     * The level is wrong in all five; nothing about the rectangle, the start index or the capacity
     * may turn that into a completed read.
     */
    void LegA2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "A2");
        if (!rt) return;
        AssertChain(*rt, true, "A2");
        const int n = rt->getLevelCountProperty();

        // A full rectangle for the level the caller THINKS exists: `max(1, 16 >> 5)` is 1, so this
        // is the largest rectangle the shared layer will accept for that level.
        const Rectangle full(0, 0, 1, 1);
        ExpectLevelRejected(*rt, n, &full, 1, kGuard, "A2 invalid level, full rectangle");

        // No rectangle at all -- the overload that defaults to the whole level.
        ExpectLevelRejected(*rt, n, nullptr, 1, kGuard, "A2 invalid level, null rectangle");

        const Rectangle sub(0, 0, 1, 1);
        ExpectLevelRejected(*rt, n, &sub, 1, 0, "A2 invalid level, startIndex 0");
        ExpectLevelRejected(*rt, n, &sub, 1, 17, "A2 invalid level, nonzero startIndex 17");

        // Mip membership precedes rectangle and positive-capacity checks. This request combines a
        // nonexistent level with an invalid rectangle and a region larger than the claimed
        // destination capacity; it must still identify `level`, with no renderer call.
        const Rectangle outside(-1, 0, 2, 2);
        ExpectLevelRejected(*rt, n, &outside, 1, kGuard,
                            "A2 invalid level combined with invalid rectangle and capacity");

        // The two validation stages that intentionally precede mip membership retain their own
        // identity. A negative destination index wins over invalid level, as does elementCount 0.
        {
            std::vector<Color> guarded(9, kSentinel);
            const std::vector<Color> before = guarded;
#if defined(CNA_RENDERER_SOFTWARE)
            auto* softwareRenderer =
                dynamic_cast<CNA::Internal::Renderers::Software::SoftwareRenderTargetRenderer*>(
                    rt->GetRenderTargetRenderer());
            const std::size_t softwareCallsBefore = softwareRenderer != nullptr
                ? softwareRenderer->GetReadbackCallCountEXT() : 0;
#endif
            bool startIndexWon = false;
            std::string startWhat;
            try { rt->GetData(n, &sub, guarded.data(), -1, 1); }
            catch (const std::out_of_range& e)
            {
                startWhat = e.what();
                startIndexWon = startWhat.find("startIndex") != std::string::npos;
            }
            catch (const std::exception& e) { startWhat = e.what(); }
            check(startIndexWon,
                  "A2 invalid level + negative startIndex: startIndex validation precedes level");

            bool elementCountWon = false;
            try { rt->GetData(n, &sub, guarded.data(), 0, 0); }
            catch (const std::invalid_argument&) { elementCountWon = true; }
            catch (const std::exception&) {}
            check(elementCountWon,
                  "A2 invalid level + elementCount 0: data/elementCount validation precedes level");
            check(guarded == before,
                  "A2 precedence rejections leave destination prefix, range and suffix unchanged");
#if defined(CNA_RENDERER_SOFTWARE)
            check(softwareRenderer != nullptr &&
                      softwareRenderer->GetReadbackCallCountEXT() == softwareCallsBefore,
                  "A2 precedence rejections make zero Software renderer readback calls");
#endif
        }

        // An undersized destination is ALSO wrong. Whichever specific error wins, the answer must
        // still be an exception that writes nothing -- never a completed read.
        std::vector<Color> tiny(1, kSentinel);
        step("A2 invalid level, undersized destination: GetData(level=" + std::to_string(n) + ")");
        bool threw = false;
        std::string what;
        try { rt->GetData(n, &sub, tiny.data(), 0, 4); }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        check(threw, "A2 invalid level, undersized destination: refused through a catchable public "
                     "exception");
        if (threw) note("A2 undersized: " + what);
        check(Exact(tiny[0], kSentinel),
              "A2 invalid level, undersized destination: nothing was written");

        ReadWholeLevel(*rt, 1, "A2 level 1 after five rejections");
    }

    /**
     * @brief A3 -- repeated invalid requests are deterministic and never poison later work.
     *
     * Ten identical refusals, then a valid read that must still be byte-exact. Pre-fix each of those
     * ten silently allocated a staging buffer, recorded a command buffer, submitted it and waited on
     * a fence for a copy the specification says is undefined.
     */
    void LegA3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "A3");
        if (!rt) return;
        const int n = rt->getLevelCountProperty();
        const Rectangle one(0, 0, 1, 1);

        for (int i = 0; i < 10; ++i)
            ExpectLevelRejected(*rt, n, &one, 1, kGuard,
                                "A3 repeated invalid request " + std::to_string(i + 1) + "/10");

        const std::vector<Color> after = ReadWholeLevel(*rt, 1, "A3 level 1 after ten rejections");
        const std::vector<Color> again = ReadWholeLevel(*rt, 1, "A3 level 1 read a second time");
        bool identical = after.size() == again.size();
        for (std::size_t i = 0; identical && i < after.size(); ++i)
            if (!Exact(after[i], again[i])) identical = false;
        check(identical, "A3: two valid reads after ten refusals are byte-identical -- a refused "
                         "request left no state behind");
    }

    /**
     * @brief B1 -- a NON-mipmapped target has exactly one valid level, and level 1 is invalid.
     *
     * This is the case that needs neither mips nor multisampling, and it is also the cleanest
     * classification instrument in the file: with `LevelCount == 1` there is only ONE level a
     * fabricated answer could have come from, so pre-fix its returned value names its own source.
     */
    void LegB1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, false, 0, "B1");
        if (!rt) return;
        AssertChain(*rt, false, "B1");

        const std::vector<Color> level0 = ReadWholeLevel(*rt, 0, "B1 the only valid level");
        if (!level0.empty())
            note("B1: level 0 texel (0,0) is " + ColorText(level0[0]) +
                 " -- the ONLY content this target owns, and therefore the only thing a fabricated "
                 "answer for level 1 could be made of");

        const Rectangle one(0, 0, 1, 1);
        ExpectLevelRejected(*rt, 1,       &one, 1, kGuard, "B1 level 1 of a single-level target");
        ExpectLevelRejected(*rt, 2,       &one, 1, kGuard, "B1 level 2 of a single-level target");
        ExpectLevelRejected(*rt, INT_MAX, &one, 1, kGuard, "B1 level INT_MAX of a single-level target");

        ReadWholeLevel(*rt, 0, "B1 level 0 after three rejections");
    }

    /**
     * @brief C1 -- the level-identity oracle: which level a fabricated answer came from.
     *
     * A 2x2 mipmapped target has exactly two levels: level 0 is the four quadrant colours, one texel
     * each, and level 1 is a single texel blending all four. Their texel (0,0) values are therefore
     * GUARANTEED different -- the blend cannot equal any single quadrant colour. That makes the two
     * candidate fabrications distinguishable by inspection: "read level 0" yields the top-left
     * quadrant colour, "clamped to the last valid level" yields the blend.
     *
     * Post-fix nothing is returned for level 2 at all, so this leg asserts the refusal; the printed
     * per-level values are what an A/B run against the unfixed renderer compares the fabricated
     * result to.
     */
    void LegC1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 2, 2, true, 0, "C1");
        if (!rt) return;
        AssertChain(*rt, true, "C1");
        const int n = rt->getLevelCountProperty();

        std::vector<Color> firstTexel;
        for (int level = 0; level < n; ++level)
        {
            const std::vector<Color> got =
                ReadLevelRect(*rt, level, 0, 0, 1, 1, 2, 2,
                              "C1 level " + std::to_string(level) + " texel (0,0)",
                              /*contentAsserted=*/false);
            if (!got.empty())
            {
                firstTexel.push_back(got[0]);
                note("C1: level " + std::to_string(level) + " texel (0,0) = " + ColorText(got[0]));
            }
        }

        if (firstTexel.size() == 2 && kTargetMipReadbackSupported && kTargetReadbackSupported)
        {
            check(!Near(firstTexel[0], firstTexel[1]),
                  "C1: level 0 and the final level differ at texel (0,0) -- so a fabricated answer "
                  "for level " + std::to_string(n) + " would NAME the level it was taken from");
            check(Near(firstTexel[0], kTL),
                  "C1: level 0 texel (0,0) is the top-left quadrant colour " + ColorText(kTL));
        }

        const Rectangle one(0, 0, 1, 1);
        ExpectLevelRejected(*rt, n, &one, 1, kGuard, "C1 level == LevelCount on a two-level chain");
    }

    /**
     * @brief D1 -- every valid level of eleven chains stays exact, and each chain's own N is refused.
     *
     * The narrow 1x13/13x1 and 2x13/13x2 pairs make each axis-bottoming orientation explicit;
     * 1x1, 2x2, 3x2, 5x3, 8x4, 13x7 and 16x16 cover single-level chains, exact powers of two, odd
     * widths, odd heights and both axes reaching 1 at different levels. The invalid index is
     * derived from each target's OWN declaration, so this cannot silently test the same number
     * twice.
     */
    void LegD1()
    {
        auto& dev = getGraphicsDeviceProperty();
        const int dims[][2] = {
            {1,1}, {1,13}, {13,1}, {2,2}, {2,13}, {13,2},
            {3,2}, {5,3}, {8,4}, {13,7}, {16,16}
        };
        for (const auto& d : dims)
        {
            const std::string label = "D1 " + std::to_string(d[0]) + "x" + std::to_string(d[1]);
            auto rt = MakeRenderedTarget(dev, d[0], d[1], true, 0, label);
            if (!rt) continue;
            AssertChain(*rt, true, label);
            ReadEveryValidLevel(*rt, label);

            const int n = rt->getLevelCountProperty();
            const Rectangle one(0, 0, 1, 1);
            ExpectLevelRejected(*rt, n, &one, 1, kGuard, label + " level == LevelCount");
        }
    }

    /**
     * @brief E1 -- the nine readback SHAPES of a valid level, so the guard costs none of them.
     *
     * REMED-GFX-149's destination window is exercised in the same leg: a nonzero start index, an
     * exactly-sized destination and an oversized one whose surplus must stay untouched.
     */
    void LegE1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "E1");
        if (!rt) return;
        if (!kTargetMipReadbackSupported || !kTargetReadbackSupported)
        {
            ReadWholeLevel(*rt, 1, "E1 level 1");
            return;
        }

        ReadWholeLevel(*rt, 1, "E1 shape 1: whole level 1");
        ReadLevelRect(*rt, 1, 0, 0, 8, 8, 16, 16, "E1 shape 2: explicit full rectangle");
        ReadLevelRect(*rt, 1, 0, 0, 4, 4, 16, 16, "E1 shape 3: sub-rectangle inside one quadrant");
        ReadLevelRect(*rt, 1, 2, 2, 4, 4, 16, 16, "E1 shape 4: sub-rectangle across the seams");
        ReadLevelRect(*rt, 1, 0, 0, 1, 1, 16, 16, "E1 shape 5: one pixel");
        ReadLevelRect(*rt, 1, 0, 7, 8, 1, 16, 16, "E1 shape 6: final row");
        ReadLevelRect(*rt, 1, 7, 0, 1, 8, 16, 16, "E1 shape 7: final column");
        ReadLevelRect(*rt, 2, 1, 0, 3, 4, 16, 16, "E1 shape 8: odd-width region of level 2");

        // Shape 9: an OVERSIZED destination -- the surplus beyond the requested window is surplus,
        // not content, and must still hold the sentinel afterwards.
        const int count = 16;
        std::vector<Color> buf(static_cast<std::size_t>(count) * 4, kSentinel);
        const Rectangle r(0, 0, 4, 4);
        step("E1 shape 9: GetData(level=1, rect=(0,0,4x4)) into a 4x oversized destination");
        rt->GetData(1, &r, buf.data(), 3, count);
        int written = 0;
        for (int i = 0; i < count; ++i)
            if (!Exact(buf[static_cast<std::size_t>(3 + i)], kSentinel)) ++written;
        check(written == count,
              "E1 shape 9: all " + std::to_string(count) + " requested elements were written (" +
                  std::to_string(written) + ")");
        bool surplusIntact = true;
        for (std::size_t i = static_cast<std::size_t>(3 + count); i < buf.size(); ++i)
            if (!Exact(buf[i], kSentinel)) { surplusIntact = false; break; }
        check(surplusIntact, "E1 shape 9: the surplus beyond the requested window is untouched");
    }

    /**
     * @brief F1 -- invalid-level rejection and valid-level exactness across sample counts.
     *
     * The APPLIED count is printed beside the requested one, because a device that applies 0 for a
     * requested 2 would otherwise make a passing leg look like coverage it is not.
     */
    void LegF1()
    {
        auto& dev = getGraphicsDeviceProperty();
        for (int samples : { 0, 1, 2, 4, 8 })
        {
            const std::string label = "F1 MSAA " + std::to_string(samples);
            auto rt = MakeRenderedTarget(dev, 16, 16, true, samples, label);
            if (!rt) continue;
            AssertChain(*rt, true, label);
            ReadEveryValidLevel(*rt, label);

            const int n = rt->getLevelCountProperty();
            const Rectangle one(0, 0, 1, 1);
            ExpectLevelRejected(*rt, n,     &one, 1, kGuard, label + " level == LevelCount");
            ExpectLevelRejected(*rt, n + 4, &one, 1, kGuard, label + " level far beyond the chain");
        }
    }

    /** @brief F2 -- the same contract for every DepthFormat a target can carry. */
    void LegF2()
    {
        auto& dev = getGraphicsDeviceProperty();
        const DepthFormat formats[] = { DepthFormat::None, DepthFormat::Depth16,
                                        DepthFormat::Depth24, DepthFormat::Depth24Stencil8 };
        const char* names[] = { "None", "Depth16", "Depth24", "Depth24Stencil8" };
        for (int i = 0; i < 4; ++i)
        {
            const std::string label = std::string("F2 ") + names[i];
            auto rt = MakeTarget(dev, 16, 16, kMipMappedTargetSupported, 0, formats[i],
                                 RenderTargetUsage::DiscardContents, label);
            if (!rt) continue;
            RenderPattern(dev, *rt);
            ReadWholeLevel(*rt, 0, label + " level 0");

            const int n = rt->getLevelCountProperty();
            const Rectangle one(0, 0, 1, 1);
            ExpectLevelRejected(*rt, n, &one, 1, kGuard, label + " level == LevelCount");
        }
    }

    /** @brief F3 -- the same contract for every RenderTargetUsage. */
    void LegF3()
    {
        auto& dev = getGraphicsDeviceProperty();
        const RenderTargetUsage usages[] = { RenderTargetUsage::DiscardContents,
                                             RenderTargetUsage::PreserveContents,
                                             RenderTargetUsage::PlatformContents };
        const char* names[] = { "DiscardContents", "PreserveContents", "PlatformContents" };
        for (int i = 0; i < 3; ++i)
        {
            const std::string label = std::string("F3 ") + names[i];
            auto rt = MakeTarget(dev, 16, 16, kMipMappedTargetSupported, 0, DepthFormat::None,
                                 usages[i], label);
            if (!rt) continue;
            RenderPattern(dev, *rt);
            ReadWholeLevel(*rt, 0, label + " level 0");

            const int n = rt->getLevelCountProperty();
            const Rectangle one(0, 0, 1, 1);
            ExpectLevelRejected(*rt, n, &one, 1, kGuard, label + " level == LevelCount");
        }
    }

    /**
     * @brief G1 -- the FIRST readback of the process is an invalid one, and valid work still works.
     *
     * A fresh process matters: pre-fix this was the request that allocated a fresh host-visible
     * staging buffer, so it is also the one whose fabricated answer could not have come from a
     * previous readback's leftovers.
     */
    void LegG1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "G1");
        if (!rt) return;
        const int n = rt->getLevelCountProperty();
        const Rectangle one(0, 0, 1, 1);

        ExpectLevelRejected(*rt, n, &one, 1, kGuard,
                            "G1 the very FIRST render-target readback of the process is invalid");
        ReadWholeLevel(*rt, 0, "G1 level 0 after the first-ever request was refused");
        ReadWholeLevel(*rt, 1, "G1 level 1 after the first-ever request was refused");
    }

    /** @brief G2 -- valid, then invalid, then valid again, on one target. */
    void LegG2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "G2");
        if (!rt) return;
        const int n = rt->getLevelCountProperty();
        const Rectangle one(0, 0, 1, 1);

        const std::vector<Color> before = ReadWholeLevel(*rt, 1, "G2 level 1 before");
        ExpectLevelRejected(*rt, n, &one, 1, kGuard, "G2 invalid between two valid reads");
        const std::vector<Color> after = ReadWholeLevel(*rt, 1, "G2 level 1 after");

        bool identical = before.size() == after.size();
        for (std::size_t i = 0; identical && i < before.size(); ++i)
            if (!Exact(before[i], after[i])) identical = false;
        check(identical, "G2: the reads either side of a refusal are byte-identical");
    }

    /** @brief G3 -- two independent targets, alternating A/B/A around refusals. */
    void LegG3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto a = MakeRenderedTarget(dev, 16, 16, true, 0, "G3 target A");
        auto b = MakeRenderedTarget(dev, 8, 4, true, 0, "G3 target B");
        if (!a || !b) return;

        const Rectangle one(0, 0, 1, 1);
        const int na = a->getLevelCountProperty();
        const int nb = b->getLevelCountProperty();

        const std::vector<Color> a1 = ReadWholeLevel(*a, 1, "G3 A level 1");
        ExpectLevelRejected(*b, nb, &one, 1, kGuard, "G3 B invalid");
        ReadWholeLevel(*b, 1, "G3 B level 1");
        ExpectLevelRejected(*a, na, &one, 1, kGuard, "G3 A invalid");
        const std::vector<Color> a2 = ReadWholeLevel(*a, 1, "G3 A level 1 again");

        bool identical = a1.size() == a2.size();
        for (std::size_t i = 0; identical && i < a1.size(); ++i)
            if (!Exact(a1[i], a2[i])) identical = false;
        check(identical, "G3: target A is byte-identical across target B's work and two refusals");

        // A refusal on one target must not have been answered from the OTHER target's image.
        ExpectLevelRejected(*b, nb + 9, &one, 1, kGuard, "G3 B far-out-of-range with A alive");
    }

    /**
     * @brief G4 -- create / read / refuse / dispose cycles, including object-address reuse.
     *
     * Four full cycles. Freeing a target and building another commonly hands back the same heap
     * address and the same native handle, which is exactly the situation in which a cached or
     * clamped level count would survive into the next target.
     */
    void LegG4()
    {
        auto& dev = getGraphicsDeviceProperty();
        const Rectangle one(0, 0, 1, 1);
        const void* firstAddress = nullptr;
        bool addressReused = false;

        for (int cycle = 0; cycle < 4; ++cycle)
        {
            const std::string label = "G4 cycle " + std::to_string(cycle);
            // Alternating sizes so a stale LevelCount from the previous cycle is a WRONG number
            // here, not an accidentally right one.
            const int edge = (cycle % 2 == 0) ? 16 : 8;
            auto rt = MakeRenderedTarget(dev, edge, edge, true, 0, label);
            if (!rt) continue;
            if (!firstAddress) firstAddress = rt.get();
            else if (firstAddress == rt.get()) addressReused = true;

            AssertChain(*rt, true, label);
            ReadWholeLevel(*rt, 1, label + " level 1");
            const int n = rt->getLevelCountProperty();
            ExpectLevelRejected(*rt, n, &one, 1, kGuard, label + " level == LevelCount");
            rt.reset();
        }
        note(std::string("G4: a later target reused the first one's object address: ") +
             (addressReused ? "yes" : "no"));
        check(true, "G4: four create/read/refuse/dispose cycles completed without a signal");
    }

    /**
     * @brief G5 -- disposal and device teardown immediately after a refusal.
     *
     * A rejected request must leave nothing queued that could write the caller's storage later, so
     * disposing the target right afterwards -- and handing another one to device teardown while it
     * has only ever been asked for an invalid level -- must be uneventful.
     */
    void LegG5()
    {
        auto& dev = getGraphicsDeviceProperty();
        const Rectangle one(0, 0, 1, 1);

        {
            auto rt = MakeRenderedTarget(dev, 16, 16, true, 0, "G5 disposed after refusal");
            if (rt)
            {
                const int n = rt->getLevelCountProperty();
                std::vector<Color> buf = SentinelBuffer(1);
                bool threw = false;
                step("G5: refuse, then dispose the target immediately");
                try { rt->GetData(n, &one, buf.data(), kGuard, 1); }
                catch (const std::exception&) { threw = true; }
                check(threw, "G5: the refusal happened");
                rt.reset();
                bool untouched = true;
                for (const Color& c : buf)
                    if (!Exact(c, kSentinel)) { untouched = false; break; }
                check(untouched, "G5: the destination is STILL untouched after the target was "
                                 "disposed -- nothing was queued to write it later");
            }
        }

        heldToTeardown_ = MakeRenderedTarget(dev, 16, 16, true, 0, "G5 held to teardown");
        if (heldToTeardown_)
        {
            const int n = heldToTeardown_->getLevelCountProperty();
            ExpectLevelRejected(*heldToTeardown_, n, &one, 1, kGuard,
                                "G5 refused, then held alive to device teardown");
        }
        check(true, "G5: reached the end of the leg without a signal");
    }

    /**
     * @brief H1 -- an MRT member's invalid level, kept strictly clear of REMED-GFX-190.
     *
     * An MRT attachment is an ordinary `RenderTarget2D` read through the very same renderer route, so
     * its invalid-level rejection IS this ticket's subject and is asserted here. Its generated mip
     * CONTENT is NOT: **REMED-GFX-190** records that Vulkan does not regenerate the PRIMARY MRT
     * attachment's chain, and this file must not claim otherwise, so only level 0 content and the
     * refusal are asserted while level 1 is measured and printed.
     */
    void LegH1()
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!kMrtSupported)
        {
            boundary("H1: this renderer does not execute multiple render targets; the invalid-level "
                     "route it shares with single targets is covered by every other leg");
            check(true, "H1: the MRT boundary is declared, not silently skipped");
            return;
        }
        auto a = MakeTarget(dev, 16, 16, kMipMappedTargetSupported, 0, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "H1 attachment 0");
        auto b = MakeTarget(dev, 16, 16, kMipMappedTargetSupported, 0, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "H1 attachment 1");
        if (!a || !b) return;

        step("H1: SetRenderTargets({a, b}), clear, draw the pattern, unbind");
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        try
        {
            std::vector<RenderTargetBinding> bindings;
            bindings.emplace_back(a.get());
            bindings.emplace_back(b.get());
            dev.SetRenderTargets(bindings);
            dev.Clear(kBase);
            DrawFlatQuad(dev, kTL, -1.f,  0.f,  0.f,  1.f);
            DrawFlatQuad(dev, kTR,  0.f,  0.f,  1.f,  1.f);
            DrawFlatQuad(dev, kBL, -1.f, -1.f,  0.f,  0.f);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }
        catch (const std::exception& e)
        {
            boundary(std::string("H1: multiple render targets are refused here: ") + e.what());
            check(true, "H1: the refusal is a catchable public exception, not an abort");
            return;
        }

        ReadWholeLevel(*a, 0, "H1 attachment 0 level 0");

        // Level 1 CONTENT belongs to REMED-GFX-190, not here: measure and print, assert only that
        // the transfer itself is complete and inside its window. Only a mip-mapped target HAS a
        // level 1 to read: a renderer whose RenderTarget2D is single-level (WEBGPU-53/54 refuses
        // mipMap=true, so kMipMappedTargetSupported is false) has no level 1, and its rejection is
        // asserted by the ExpectLevelRejected(level == LevelCount) check below instead.
        if (kMipMappedTargetSupported)
            ReadWholeLevel(*a, 1, "H1 attachment 0 level 1", /*contentAsserted=*/false);

        const Rectangle one(0, 0, 1, 1);
        const int na = a->getLevelCountProperty();
        const int nb = b->getLevelCountProperty();
        ExpectLevelRejected(*a, na, &one, 1, kGuard, "H1 MRT attachment 0 level == LevelCount");
        ExpectLevelRejected(*b, nb, &one, 1, kGuard, "H1 MRT attachment 1 level == LevelCount");
        boundary("H1: MRT mip CONTENT is REMED-GFX-190's subject and is deliberately NOT claimed "
                 "here -- only the invalid-level rejection, which shares this ticket's route");
    }

    // ================================================================ the runner

    template <typename M>
    void runLeg(const char* id, M method)
    {
        if (!wants(id)) return;
        try
        {
            (this->*method)();
        }
        catch (const std::exception& e)
        {
            check(false, std::string("leg ") + id + " threw an unexpected exception: " + e.what());
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        runLeg("A1", &RenderTargetInvalidMipLevelTest::LegA1);
        runLeg("A2", &RenderTargetInvalidMipLevelTest::LegA2);
        runLeg("A3", &RenderTargetInvalidMipLevelTest::LegA3);

        runLeg("B1", &RenderTargetInvalidMipLevelTest::LegB1);

        runLeg("C1", &RenderTargetInvalidMipLevelTest::LegC1);

        runLeg("D1", &RenderTargetInvalidMipLevelTest::LegD1);

        runLeg("E1", &RenderTargetInvalidMipLevelTest::LegE1);

        runLeg("F1", &RenderTargetInvalidMipLevelTest::LegF1);
        runLeg("F2", &RenderTargetInvalidMipLevelTest::LegF2);
        runLeg("F3", &RenderTargetInvalidMipLevelTest::LegF3);

        runLeg("G1", &RenderTargetInvalidMipLevelTest::LegG1);
        runLeg("G2", &RenderTargetInvalidMipLevelTest::LegG2);
        runLeg("G3", &RenderTargetInvalidMipLevelTest::LegG3);
        runLeg("G4", &RenderTargetInvalidMipLevelTest::LegG4);
        runLeg("G5", &RenderTargetInvalidMipLevelTest::LegG5);

        runLeg("H1", &RenderTargetInvalidMipLevelTest::LegH1);

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
    explicit RenderTargetInvalidMipLevelTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        // REMED-GFX-191: Vulkan's render-target sample count follows the device-wide count. F1
        // must therefore engage multisampling before device creation; target constructor requests
        // alone would all report applied 0 and leave the nominal MSAA matrix on the single-sample
        // path. F1 still requests 0 and 1 as explicit non-MSAA controls, and now reads every valid
        // mip for both those targets and each actually multisampled target the renderer supports.
        if (onlyLeg_ == "F1") gdm_->setPreferMultiSamplingProperty(true);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /// Every leg, in the order the supervisor runs them. NONE of them may abort.
    const char* const kLegs[] = {
        "A1", "A2", "A3",
        "B1",
        "C1",
        "D1",
        "E1",
        "F1", "F2", "F3",
        "G1", "G2", "G3", "G4", "G5",
        "H1",
    };

#if CNA_GFX189_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever.
    constexpr unsigned kLegTimeoutSeconds = 240;

    /**
     * @brief Runs one leg in a child process and classifies the outcome exactly.
     *
     * "The FIRST readback of the process is invalid" (leg G1) is a property only a fresh process can
     * establish, and the sibling defect this file generalises killed its process outright, so a leg
     * that dies must be reported as a leg rather than take the whole run with it.
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
            else if (sig == SIGSEGV)
                std::printf("[FATAL] leg %s: SIGSEGV%s -- an out-of-range level reached native code "
                            "and dereferenced it; the last [STEP] line names the public call\n",
                            legId, core);
            else if (sig == SIGABRT)
                std::printf("[FATAL] leg %s: SIGABRT%s -- std::terminate or abort()\n", legId, core);
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

#if CNA_GFX189_CAN_FORK
    if (onlyLeg.empty())
    {
        const int total = static_cast<int>(sizeof(kLegs) / sizeof(kLegs[0]));
        std::printf("[INFO] REMED-GFX-189 supervisor on %s: %d legs, each in its own process\n",
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

    RenderTargetInvalidMipLevelTest game(std::move(onlyLeg));
    game.Run();
    return game.Result();
}
