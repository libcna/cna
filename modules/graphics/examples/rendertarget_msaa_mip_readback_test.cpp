// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-186 -- reading a NONZERO MIP LEVEL of a mipmapped MULTISAMPLED `RenderTarget2D` must
// return that level's generated content, and must never kill the process.
//
// THE DEFECT
// ----------
// On SDL_GPU the public sequence
//
//     RenderTarget2D rt(dev, 16, 16, /*mipMap=*/true, Color, DepthFormat::None, 4, DiscardContents);
//     dev.SetRenderTarget(&rt);
//     dev.Clear(base);                    // nonzero, asymmetric
//     ... draw an asymmetric pattern ...
//     dev.SetRenderTarget(nullptr);
//     rt.GetData(0, &full, dst, guard, 16 * 16);   // exact -- the positive control
//     rt.GetData(1, &half, dst, guard,  8 *  8);   // <-- SIGSEGV, exit 139, core dumped
//
// died by an UNCATCHABLE signal, so a game could not degrade gracefully. Level 0 of the very same
// target read back exactly first, which is what makes this the MIP-LEVEL route rather than the
// multisample resolve (REMED-GFX-154) or the transfer window (REMED-GFX-149).
//
// THE PUBLIC CONTRACT THIS FILE ENCODES
// -------------------------------------
// `mipMap` and `multiSampleCount` are INDEPENDENT in XNA. FNA's `RenderTarget2D` forwards `mipMap`
// to its `Texture2D` base regardless of `preferredMultiSampleCount`, so `LevelCount` is the full
// `CalculateMipLevels(width, height)` chain either way, and FNA3D carries `levelCount` and
// `multiSampleCount` side by side in one `FNA3D_RenderTargetBinding`. `FNA3D_ResolveTarget`, which
// runs when a target is UNBOUND, does both jobs in the one required order:
//
//   1. the multisample colour buffer resolves into the SINGLE-SAMPLE texture's level 0
//      (`OPENGL_ResolveTarget`'s `glBlitFramebuffer`; under SDL_GPU the render pass' own
//      `resolve_texture` store action);
//   2. then, `if (levelCount > 1)`, the WHOLE CHAIN is regenerated from that resolved level 0
//      (`glGenerateMipmap` / `SDL_GenerateMipmapsForGPUTexture`).
//
// FNA3D's own SDL_GPU driver states it outright: `SDLGPU_ResolveTarget` returns early only when
// `texture->createInfo.num_levels <= 1` ("Nothing to do, SDL_GPU resolves MSAA for us"), and
// otherwise ends the render pass and calls `SDL_GenerateMipmapsForGPUTexture` on the single-sample
// texture. The MULTISAMPLE attachment is a separate 1-level `COLOR_TARGET`-only resource
// (`SDLGPU_GenColorRenderbuffer`) and never owns level 1 at all.
//
// So, for `mipMap=true, multiSampleCount>1`:
//   * public levels           -- `CalculateMipLevels(w, h)`, unreduced by MSAA;
//   * level 0                 -- resolved from the multisample attachment when the target unbinds;
//   * levels 1..N-1           -- GENERATED from the resolved level 0, at the same moment;
//   * `GetData(level > 0)`    -- supported, synchronous, and exact;
//   * every level             -- owned by the SINGLE-SAMPLE resolved texture;
//   * `level >= LevelCount`   -- a caller error, rejected deterministically before any native call.
//
// CNA's own conforming renderers already implement exactly this: `VulkanTargetPassEXT::Mip`-
// `GenerateMips` regenerates "from level 0's just-rendered (and, where MSAA was engaged,
// just-resolved) content" right after the render pass ends, and EasyGL regenerates on unbind.
//
// THE ORACLE
// ----------
// The level-0 image is four flat quadrants in four colours chosen so that NO two are within
// tolerance on any channel and EVERY channel of every colour is nonzero. Because each quadrant
// boundary sits on an even texel index, a correctly generated level L is that SAME four-quadrant
// image at that level's own size -- not a blur of it -- for every level whose texels do not
// straddle a seam. That makes the oracle falsify three separate wrong answers at once:
//
//   * "wrote nothing"        -- the window still holds a sentinel colour absent from the image;
//   * "wrote zeroes"         -- (0,0,0,0) is not a colour any level can legitimately contain;
//   * "read level 0 instead" -- a level-0 read of the same rectangle is ENTIRELY the top-left
//                               quadrant colour, so it cannot pass a four-quadrant check.
//
// A texel whose level-0 footprint straddles a quadrant seam is a genuine blend, so it is checked
// against the mathematically bounded channel envelope of the contributing colours instead of an
// exact value -- and the FINAL 1x1 level of a chain is additionally required to differ from EVERY
// single quadrant colour, which proves more than one source region contributed.
//
// PROCESS ISOLATION
// -----------------
// Each leg runs in its own forked child, so the SIGSEGV this ticket is about cannot destroy the
// other results and is classified exactly (signal, core, and the last `[STEP]` line naming the
// public call). No leg is permitted to abort.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "common/PixelTestGame.hpp"

#include <algorithm>
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
#define CNA_GFX186_CAN_FORK 1
#else
#define CNA_GFX186_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRT  = 16;   ///< Canonical render-target edge; quadrants are 8x8, chain is 5 deep.
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
#error "REMED-GFX-186: this renderer has no declared target mip-readback contract."
#endif

    /**
     * @brief Whether this renderer can read a render target's colour attachment back at all.
     *
     * False on HEADLESS, which rasterizes nothing and therefore owns no colour to return. Its
     * `GetData` is REMED-GFX-127/130's deterministic public REFUSAL, and that refusal is what this
     * file asserts there: it must throw, and it must leave the destination completely untouched.
     * A refusal that quietly wrote zeroes would be indistinguishable from a fabricated read.
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
     * True everywhere. This was false on WEBGPU while its `RenderTarget2D` constructor raised a
     * catchable `std::runtime_error` for `mipMap=true`; `plans/plan_webgpu.md` `WEBGPU-164`
     * allocates the chain and regenerates it from level 0 on unbind, the same timing FNA3D's
     * `ResolveTarget` uses, so the legs below now measure content on that renderer instead of
     * measuring a refusal.
     */
    constexpr bool kMipMappedTargetSupported = true;

    /**
     * @brief Whether GENERATED mip CONTENT of a render target is asserted on this renderer.
     *
     * The transfer contract -- the call returns, every requested element is written, the protected
     * prefix and suffix survive, nothing crashes -- is asserted EVERYWHERE. Content is asserted
     * everywhere it is measurable; a renderer that cannot regenerate a target's chain prints what it
     * actually measured instead of skipping, so a stale declaration cannot outlive the defect.
     */
    constexpr bool kGeneratedMipContentAsserted = true;

    /**
     * @brief Whether this renderer POPULATES a render target's levels above zero at all.
     *
     * False on SOFTWARE, whose `SoftwareRenderTargetRenderer::GetData` raises
     * "the Software renderer stores mip level 0 only; level N was requested" -- a specific,
     * catchable public refusal, with `LevelCount` still correct and level 0 still byte-exact.
     * That is that renderer's own declared boundary, not this ticket's subject, and it is ASSERTED
     * here rather than skipped: a nonzero level must throw and must leave the destination
     * completely untouched. If Software ever grows a real chain, this check turns red and says so.
     *
     * False on HEADLESS too, which rasterizes nothing and refuses every render-target readback
     * (`kTargetReadbackSupported`), so the refusal is already asserted one level up.
     */
    constexpr bool kTargetMipReadbackSupported =
#if defined(CNA_RENDERER_SOFTWARE)
        false;
#else
        true;
#endif

    /** @brief Whether `SetRenderTargets` with more than one attachment is executed here. */
    constexpr bool kMrtSupported =
#if defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    /**
     * @brief Whether the CONTENT of an MRT extra attachment's generated level is claimable here.
     *
     * SDL_GPU and Vulkan leave attachment 1 at the pass' explicit clear colour when a stock
     * single-output draw writes attachment 0 only, so its regenerated chain is independently
     * claimable there. XNA leaves a genuinely unwritten MRT output undefined, so everywhere else
     * this file measures and prints what the extra attachment holds rather than inventing a value.
     */
    constexpr bool kMrtExtraAttachmentContentClaimable =
#if defined(CNA_RENDERER_SDL_GPU) || defined(CNA_RENDERER_VULKAN)
        true;
#else
        false;
#endif

    /** @brief Whether `RenderTargetCube` is a bindable render target here. */
    constexpr bool kCubeTargetSupported =
#if defined(CNA_RENDERER_SOFTWARE)
        false;
#else
        true;
#endif

    // ---- the asymmetric pattern -------------------------------------------------------------
    //
    // Unique corners, distinct rows AND columns, one mid-tone, nonzero alpha, and -- deliberately
    // -- every channel of every colour nonzero, so a single zeroed channel is a visible failure
    // rather than something a tolerance could absorb. Any two differ by >= 90 on some channel, far
    // outside kTol, so a swapped quadrant can never read as a rounding miss.
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

    /**
     * @brief True when (@p x, @p y) of the LEVEL-0 image is far enough from a quadrant SEAM to be
     * free of any multisample blend.
     *
     * Only the two interior seams are excluded. The image's OUTER border is deliberately NOT
     * excluded: every quad spans its half of NDC exactly, so the outermost pixel row and column are
     * fully covered and resolve to the flat quadrant colour. Excluding them too would make a
     * `final row` rectangle check vacuous -- every texel skipped, and a check with nothing to
     * compare passes.
     */
    bool Interior(int x, int y, int w, int h)
    {
        const int dx = x < w / 2 ? (w / 2 - 1 - x) : (x - w / 2);
        const int dy = y < h / 2 ? (h / 2 - 1 - y) : (y - h / 2);
        return dx >= 2 && dy >= 2;
    }

    /**
     * @brief The exact expected colour of level-@p level texel (@p x, @p y), or false if it blends.
     *
     * A generated mip texel averages its own footprint in the level-0 image. When that whole
     * footprint lies inside ONE quadrant AND every level-0 texel of it is `Interior`, the average
     * IS that quadrant's flat colour and can be asserted exactly. Otherwise the texel is a genuine
     * blend of two or four quadrants (or of multisample-softened seam texels) and only the bounded
     * envelope below can be claimed about it.
     *
     * @param x,y    the texel, in level-@p level coordinates.
     * @param level  the mip level.
     * @param w0,h0  the LEVEL-0 dimensions the pattern is defined over.
     * @param out    receives the exact colour when this returns true.
     */
    bool ExactAtLevel(int x, int y, int level, int w0, int h0, Color& out)
    {
        const int span = 1 << level;
        const int x0 = x * span;
        const int y0 = y * span;
        // A footprint that runs off the level-0 image (an odd chain halves by truncation) is never
        // a clean average of a single quadrant.
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

    /**
     * @brief The mathematically bounded channel envelope every legitimate generated texel lies in.
     *
     * Any average of the four palette colours -- whatever weights a driver's downsample uses --
     * lies between their per-channel minimum and maximum. A texel outside it is not a blend of this
     * image at all, so this stays a real constraint even where an exact value is not derivable.
     */
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
 * @brief REMED-GFX-186's contract: reading a generated mip level of a multisampled render target.
 */
class RenderTargetMsaaMipReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Leg G2's target: read once, then handed to device teardown still alive.
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
     * @brief Runs @p doRead, or asserts this renderer's deterministic refusal instead.
     *
     * On a renderer that cannot read a render target back, the ONLY correct behaviour is a public
     * exception that writes nothing -- so that is what gets asserted, using the same sentinel
     * buffer. A refusal that silently wrote zeroes would look exactly like a fabricated read.
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
     * @brief Reads one rectangle of one mip level and asserts the whole contract for it.
     *
     * Issues EXACTLY ONE `GetData`, into a sentinel-prefilled destination with a protected prefix
     * and suffix, and asserts:
     *   * every requested element was overwritten;
     *   * the guards survived;
     *   * no texel is exactly (0,0,0,0) -- no palette colour is, so that value can only be
     *     fabricated or uninitialised;
     *   * every texel lies inside the palette's bounded channel envelope;
     *   * every texel whose level-0 footprint is a clean single-quadrant block equals that
     *     quadrant's colour exactly -- which a read of the WRONG level cannot satisfy, because a
     *     level-0 read of the same rectangle is entirely one quadrant.
     *
     * @param rx,ry,rw,rh the requested rectangle in LEVEL coordinates.
     * @param w0,h0       the level-0 dimensions the pattern is defined over.
     * @return the texels read, so a caller can compare two reads byte for byte.
     */
    std::vector<Color> ReadLevelRect(RenderTarget2D& rt, int level, int rx, int ry, int rw, int rh,
                                     int w0, int h0, const std::string& label,
                                     bool contentAsserted = kGeneratedMipContentAsserted)
    {
        const int count = rw * rh;
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle r(rx, ry, rw, rh);

        // A renderer that populates level 0 only owes a DETERMINISTIC REFUSAL for every other
        // level, and owes it without touching the destination -- a refusal that quietly wrote
        // zeroes would be indistinguishable from an ungenerated chain being handed back as content.
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
        bool sawTL = false, sawTR = false, sawBL = false, sawBase = false;

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
                    else
                    {
                        if (Exact(want, kTL))   sawTL   = true;
                        if (Exact(want, kTR))   sawTR   = true;
                        if (Exact(want, kBL))   sawBL   = true;
                        if (Exact(want, kBase)) sawBase = true;
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
            boundary(label + ": generated mip CONTENT is not asserted on this renderer -- measured " +
                     std::to_string(zeroed) + "/" + std::to_string(count) +
                     " texels exactly (0,0,0,0), " + std::to_string(exactWrong) + "/" +
                     std::to_string(exactChecked) + " derivable texels wrong, first " +
                     (firstMismatch.empty() ? std::string("none") : firstMismatch));
            return std::vector<Color>(buf.begin() + kGuard, buf.begin() + kGuard + count);
        }

        check(zeroed == 0,
              label + ": no texel is exactly (0,0,0,0) (" + std::to_string(zeroed) + "/" +
                  std::to_string(count) + " were) -- every palette colour has all four channels "
                  "nonzero, so that value can only be fabricated");
        check(outsideEnvelope == 0,
              label + ": every texel lies inside the palette's bounded channel envelope (" +
                  std::to_string(outsideEnvelope) + "/" + std::to_string(count) + " did not)");
        if (exactChecked > 0)
        {
            if (exactWrong != 0)
                std::printf("        %d/%d derivable texels wrong; first %s\n", exactWrong,
                            exactChecked, firstMismatch.c_str());
            check(exactWrong == 0,
                  label + ": all " + std::to_string(exactChecked) +
                      " texels whose level-0 footprint is a single clean quadrant hold that "
                      "quadrant's colour");
        }
        else
        {
            note(label + ": no texel of this rectangle has a derivable exact value (every footprint "
                         "straddles a seam) -- the envelope and zero checks above are the claim");
        }

        // "All expected source regions contribute": only meaningful when the requested rectangle
        // actually spans more than one quadrant of the level.
        const int lw = LevelDim(w0, level);
        const int lh = LevelDim(h0, level);
        if (rx == 0 && ry == 0 && rw == lw && rh == lh && exactChecked > 0 && lw >= 4 && lh >= 4)
        {
            const bool all = sawTL && sawTR && sawBL && sawBase;
            if (!all)
                std::printf("        contributing quadrants: TL=%d TR=%d BL=%d Base=%d\n",
                            sawTL ? 1 : 0, sawTR ? 1 : 0, sawBL ? 1 : 0, sawBase ? 1 : 0);
            check(all, label + ": all four source quadrants are present in this level -- a read of "
                               "the WRONG level would hold only one of them");
        }

        return std::vector<Color>(buf.begin() + kGuard, buf.begin() + kGuard + count);
    }

    /** @brief Reads the COMPLETE mip level @p level and asserts the contract for it. */
    std::vector<Color> ReadWholeLevel(RenderTarget2D& rt, int level, const std::string& label,
                                      bool contentAsserted = kGeneratedMipContentAsserted)
    {
        const int w0 = rt.getWidthProperty();
        const int h0 = rt.getHeightProperty();
        return ReadLevelRect(rt, level, 0, 0, LevelDim(w0, level), LevelDim(h0, level), w0, h0,
                             label, contentAsserted);
    }

    /**
     * @brief Asserts a level that does not exist is REJECTED deterministically, writing nothing.
     *
     * A public exception is the only acceptable answer -- never a signal, and never a quietly
     * fabricated buffer. This is the check the SIGSEGV under investigation replaced.
     */
    void ExpectLevelRejected(RenderTarget2D& rt, int level, const std::string& label)
    {
        const int count = 4;
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle r(0, 0, 1, 1);
        step(label + ": GetData(level=" + std::to_string(level) + ") must be REFUSED before any "
             "native call");
        bool threw = false;
        std::string what;
        try { rt.GetData(level, &r, buf.data(), kGuard, count); }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        bool untouched = true;
        for (const Color& c : buf)
            if (!Exact(c, kSentinel)) { untouched = false; break; }

        // REMED-GFX-189 CLOSED (2026-07-31): Vulkan used to answer an out-of-range level with
        // fabricated level-0 content instead of refusing, so this file carried a
        // `kOutOfRangeLevelRejected` carve-out asserting that defect. The carve-out is DELETED
        // rather than flipped, so every renderer now runs the same unconditional assertion here and
        // a renderer that regresses fails outright instead of quietly matching a declaration.
        check(threw, label + ": rejected through a catchable public exception, not a signal");
        if (threw) note(label + ": " + what);
        check(untouched, label + ": the refused read left the whole destination untouched");
    }

    /**
     * @brief Builds a target, or declares this renderer's catchable refusal and returns null.
     */
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
                  " -- mipMap and multiSampleCount are INDEPENDENT in XNA, so multisampling must "
                  "not shorten the public chain");
    }

    // ================================================================ Group A: the reproducer

    /**
     * @brief A1 -- THE reproducer, verbatim: 16x16 mipMap MSAA=4, level 0, then level 1 ONCE.
     */
    void LegA1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A1");
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, "A1");
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, "A1 level 0 (the positive control)");
        // EXACTLY ONE level-1 read, immediately after, with nothing in between: no Present, no
        // second frame, no dummy bind, no retry, no sleep.
        ReadWholeLevel(*rt, 1, "A1 level 1 (the subject)");
    }

    /** @brief A2 -- the complete chain of the canonical target, every level, each read once. */
    void LegA2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A2");
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, "A2");
        RenderPattern(dev, *rt);
        const int levels = rt->getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
            ReadWholeLevel(*rt, level, "A2 level " + std::to_string(level));

        // The FINAL 1x1 level must be a blend of more than one source region -- if it equalled any
        // single quadrant colour the chain would have collapsed rather than been generated.
        if (levels >= 2 && kTargetReadbackSupported && kTargetMipReadbackSupported)
        {
            std::vector<Color> last = ReadWholeLevel(*rt, levels - 1,
                                                     "A2 final level re-read for the blend check");
            const Color c = last.front();
            const bool blended = !Near(c, kTL) && !Near(c, kTR) && !Near(c, kBL) && !Near(c, kBase);
            note("A2 final 1x1 level = " + ColorText(c));
            check(blended, "A2: the final 1x1 level differs from EVERY single quadrant colour, so "
                           "more than one source region contributed to it");
        }
    }

    /** @brief A3 -- an ODD, non-power-of-two chain: 13x7, mipMap, MSAA=4, every level. */
    void LegA3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, 13, 7, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A3");
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, "A3");
        RenderPattern(dev, *rt);
        const int levels = rt->getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
            ReadWholeLevel(*rt, level, "A3 13x7 level " + std::to_string(level));
    }

    /** @brief A4 -- level EXACTLY equal to LevelCount must reject, not crash. */
    void LegA4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A4");
        if (!rt) return;
        RenderPattern(dev, *rt);
        const int levels = rt->getLevelCountProperty();
        ExpectLevelRejected(*rt, levels, "A4 level == LevelCount");
        ExpectLevelRejected(*rt, levels + 7, "A4 level far beyond the chain");
        // The target is still usable afterwards -- a rejection must not have damaged it.
        ReadWholeLevel(*rt, 1, "A4 level 1 after two rejections");
    }

    /** @brief A5 -- a NEGATIVE level must reject through the public argument contract. */
    void LegA5()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A5");
        if (!rt) return;
        RenderPattern(dev, *rt);
        ExpectLevelRejected(*rt, -1, "A5 level -1");
        ReadWholeLevel(*rt, 0, "A5 level 0 after the rejection");
    }

    /** @brief A6 -- the SECOND level-1 read must equal the first, byte for byte. */
    void LegA6()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A6");
        if (!rt) return;
        RenderPattern(dev, *rt);
        std::vector<Color> first  = ReadWholeLevel(*rt, 1, "A6 first level-1 read");
        std::vector<Color> second = ReadWholeLevel(*rt, 1, "A6 second level-1 read");
        bool same = first.size() == second.size();
        for (std::size_t i = 0; same && i < first.size(); ++i)
            same = Exact(first[i], second[i]);
        check(same, "A6: the two level-1 reads are byte-identical -- repetition changes nothing");
    }

    /** @brief A7 -- level 1 FIRST, then level 0: order must not matter. */
    void LegA7()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "A7");
        if (!rt) return;
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 1, "A7 level 1 read FIRST in the process");
        ReadWholeLevel(*rt, 0, "A7 level 0 read after it");
    }

    /** @brief A8 -- two targets, read A1, B1, A1, B1: neither may depend on the other. */
    void LegA8()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto a = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "A8 target A");
        if (!a) return;
        auto b = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "A8 target B");
        if (!b) return;
        RenderPattern(dev, *a);
        RenderPattern(dev, *b);
        ReadWholeLevel(*a, 1, "A8 A level 1 first read");
        ReadWholeLevel(*b, 1, "A8 B level 1 first read");
        ReadWholeLevel(*a, 1, "A8 A level 1 second read");
        ReadWholeLevel(*b, 1, "A8 B level 1 second read");
    }

    // ================================================================ Group B: sample counts

    void SampleLeg(const char* id, int requested)
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::string label = std::string(id) + " requested MSAA " + std::to_string(requested);
        auto rt = MakeTarget(dev, kRT, kRT, true, requested, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, label);
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, label);
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, label + " level 0");
        ReadWholeLevel(*rt, 1, label + " level 1");
    }

    void LegB0()  { SampleLeg("B0",  0);  }
    void LegB1()  { SampleLeg("B1",  1);  }
    void LegB2()  { SampleLeg("B2",  2);  }
    void LegB4()  { SampleLeg("B4",  4);  }
    void LegB8()  { SampleLeg("B8",  8);  }
    void LegB16() { SampleLeg("B16", 16); }
    void LegB64() { SampleLeg("B64", 64); }

    // ================================================================ Group C: what the defect needs

    /** @brief C1 -- a mipmapped target WITHOUT multisampling: does level 1 need MSAA to fail? */
    void LegC1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, true, 0, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "C1 mipmapped, NOT multisampled");
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, "C1");
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, "C1 level 0");
        ReadWholeLevel(*rt, 1, "C1 level 1");
    }

    /** @brief C2 -- MULTISAMPLED but NOT mipmapped: LevelCount is 1, so level 1 must reject. */
    void LegC2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, false, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "C2 multisampled, NOT mipmapped");
        if (!rt) return;
        AssertChain(*rt, false, "C2");
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, "C2 level 0");
        ExpectLevelRejected(*rt, 1, "C2 level 1 of a single-level target");
    }

    /** @brief C3 -- neither mipmapped nor multisampled: level 1 must still reject, not crash. */
    void LegC3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, kRT, kRT, false, 0, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "C3 plain target");
        if (!rt) return;
        AssertChain(*rt, false, "C3");
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, "C3 level 0");
        ExpectLevelRejected(*rt, 1, "C3 level 1 of a single-level target");
    }

    // ================================================================ Group D: depth formats

    void DepthLeg(const char* id, DepthFormat depth, const char* name)
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::string label = std::string(id) + " " + name;
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, depth,
                             RenderTargetUsage::DiscardContents, label);
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, label);
        RenderPattern(dev, *rt);
        // The COLOUR chain must be what comes back -- a depth attachment must never become the
        // readback source, at any level.
        ReadWholeLevel(*rt, 0, label + " level 0");
        ReadWholeLevel(*rt, 1, label + " level 1");
    }

    void LegD1() { DepthLeg("D1", DepthFormat::None,            "DepthFormat::None"); }
    void LegD2() { DepthLeg("D2", DepthFormat::Depth16,         "DepthFormat::Depth16"); }
    void LegD3() { DepthLeg("D3", DepthFormat::Depth24,         "DepthFormat::Depth24"); }
    void LegD4() { DepthLeg("D4", DepthFormat::Depth24Stencil8, "DepthFormat::Depth24Stencil8"); }

    // ================================================================ Group E: target usage

    void UsageLeg(const char* id, RenderTargetUsage usage, const char* name)
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::string label = std::string(id) + " " + name;
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None, usage, label);
        if (!rt) return;
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, 0, label + " level 0");
        ReadWholeLevel(*rt, 1, label + " level 1");
    }

    void LegE1() { UsageLeg("E1", RenderTargetUsage::DiscardContents,  "DiscardContents"); }
    void LegE2() { UsageLeg("E2", RenderTargetUsage::PreserveContents, "PreserveContents"); }
    void LegE3() { UsageLeg("E3", RenderTargetUsage::PlatformContents, "PlatformContents"); }

    // ================================================================ Group F: readback shapes

    /** @brief Builds the canonical target and renders it, for the shape legs. */
    std::unique_ptr<RenderTarget2D> ShapeTarget(GraphicsDevice& dev, const char* id)
    {
        auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, id);
        if (rt) RenderPattern(dev, *rt);
        return rt;
    }

    /** @brief F1 -- the whole of level 1 through an EXPLICIT full rectangle. */
    void LegF1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F1");
        if (!rt) return;
        ReadLevelRect(*rt, 1, 0, 0, kRT / 2, kRT / 2, kRT, kRT, "F1 explicit full level-1 rectangle");
    }

    /** @brief F2 -- a sub-rectangle of level 1, wholly inside one quadrant and across two. */
    void LegF2()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F2");
        if (!rt) return;
        ReadLevelRect(*rt, 1, 0, 0, 2, 2, kRT, kRT, "F2 level-1 sub-rectangle inside one quadrant");
        ReadLevelRect(*rt, 1, 2, 2, 4, 4, kRT, kRT, "F2 level-1 sub-rectangle across the seams");
    }

    /** @brief F3 -- ONE pixel of level 1, and one of level 2. */
    void LegF3()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F3");
        if (!rt) return;
        ReadLevelRect(*rt, 1, 1, 1, 1, 1, kRT, kRT, "F3 one pixel of level 1");
        ReadLevelRect(*rt, 2, 0, 0, 1, 1, kRT, kRT, "F3 one pixel of level 2");
    }

    /** @brief F4 -- the FINAL row of level 1, the row an off-by-one drops. */
    void LegF4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F4");
        if (!rt) return;
        const int lw = kRT / 2, lh = kRT / 2;
        ReadLevelRect(*rt, 1, 0, lh - 1, lw, 1, kRT, kRT, "F4 final row of level 1");
        ReadLevelRect(*rt, 1, lw - 1, 0, 1, lh, kRT, kRT, "F4 final column of level 1");
    }

    /** @brief F5 -- an ODD-width, odd-height region of level 1. */
    void LegF5()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F5");
        if (!rt) return;
        ReadLevelRect(*rt, 1, 1, 1, 3, 5, kRT, kRT, "F5 odd 3x5 region of level 1");
    }

    /** @brief F6 -- a nonzero `startIndex` with a larger-than-needed destination. */
    void LegF6()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F6");
        if (!rt) return;
        if (!kTargetMipReadbackSupported)
        {
            // Same declared boundary as every other nonzero-level read here; the oversized-window
            // contract is exercised at level 0 by REMED-GFX-149's own fixture on this renderer.
            ReadLevelRect(*rt, 1, 0, 0, kRT / 2, kRT / 2, kRT, kRT, "F6 level 1");
            return;
        }
        const int lw = kRT / 2, lh = kRT / 2;
        const int count = lw * lh;
        // Deliberately oversized: REMED-GFX-149's contract says a capacity larger than the region
        // is legal and the surplus must be left untouched.
        const int capacity = count + 37;
        std::vector<Color> buf(static_cast<std::size_t>(kGuard + capacity + kGuard), kSentinel);
        const Rectangle r(0, 0, lw, lh);
        step("F6: GetData(level=1, full rect, startIndex=" + std::to_string(kGuard) +
             ", elementCount=" + std::to_string(capacity) + ") into an OVERSIZED destination");
        if (!IssueRead([&] { rt->GetData(1, &r, buf.data(), kGuard, capacity); }, buf, "F6"))
            return;

        int untouched = 0;
        for (int i = 0; i < count; ++i)
            if (Exact(buf[static_cast<std::size_t>(kGuard + i)], kSentinel)) ++untouched;
        check(untouched == 0, "F6: every one of the " + std::to_string(count) +
                                  " region elements was written from startIndex " +
                                  std::to_string(kGuard));
        bool tailIntact = true;
        for (int i = count; i < capacity + kGuard; ++i)
            if (!Exact(buf[static_cast<std::size_t>(kGuard + i)], kSentinel)) { tailIntact = false; break; }
        check(tailIntact, "F6: the surplus capacity beyond the region is left untouched");
        bool prefixIntact = true;
        for (int i = 0; i < kGuard; ++i)
            if (!Exact(buf[static_cast<std::size_t>(i)], kSentinel)) { prefixIntact = false; break; }
        check(prefixIntact, "F6: the protected prefix before startIndex is untouched");
    }

    /** @brief F7 -- an elementCount EXACTLY equal to the region, no slack at all. */
    void LegF7()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F7");
        if (!rt) return;
        ReadLevelRect(*rt, 1, 2, 1, 3, 3, kRT, kRT, "F7 exact elementCount for a 3x3 region");
    }

    /** @brief F8 -- a destination ONE element too small must be refused, writing nothing. */
    void LegF8()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F8");
        if (!rt) return;
        const int lw = kRT / 2, lh = kRT / 2;
        const int count = lw * lh;
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle r(0, 0, lw, lh);
        step("F8: GetData(level=1, full rect, elementCount=" + std::to_string(count - 1) +
             ") is one element short and must be REFUSED");
        bool threw = false;
        try { rt->GetData(1, &r, buf.data(), kGuard, count - 1); }
        catch (const std::exception&) { threw = true; }
        check(threw, "F8: a one-element-short destination is refused through a public exception");
        bool untouched = true;
        for (const Color& c : buf)
            if (!Exact(c, kSentinel)) { untouched = false; break; }
        check(untouched, "F8: the refused read wrote nothing at all");
    }

    /** @brief F9 -- a rectangle valid at level 0 but OUT OF BOUNDS at level 1 must be refused. */
    void LegF9()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F9");
        if (!rt) return;
        const int count = kRT * kRT;
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle full(0, 0, kRT, kRT);
        step("F9: GetData(level=1, rect=(0,0,16x16)) exceeds the 8x8 level and must be REFUSED");
        bool threw = false;
        try { rt->GetData(1, &full, buf.data(), kGuard, count); }
        catch (const std::exception&) { threw = true; }
        check(threw, "F9: a rectangle sized from level 0 is refused at level 1");
        bool untouched = true;
        for (const Color& c : buf)
            if (!Exact(c, kSentinel)) { untouched = false; break; }
        check(untouched, "F9: the refused read wrote nothing at all");
    }

    /** @brief F10 -- the whole-level 2-argument overload still reads level 0. */
    void LegF10()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "F10");
        if (!rt) return;
        const int count = kRT * kRT;
        std::vector<Color> buf = SentinelBuffer(count);
        step("F10: GetData(dst, startIndex, elementCount) -- the whole-level-0 overload");
        if (!IssueRead([&] { rt->GetData(buf.data(), kGuard, count); }, buf, "F10"))
            return;
        int untouched = 0, wrong = 0;
        std::string firstMismatch;
        for (int y = 0; y < kRT; ++y)
            for (int x = 0; x < kRT; ++x)
            {
                const Color got = buf[static_cast<std::size_t>(kGuard + y * kRT + x)];
                if (Exact(got, kSentinel)) { ++untouched; continue; }
                if (!Interior(x, y, kRT, kRT)) continue;
                if (!Near(got, Expected(x, y, kRT, kRT)))
                {
                    if (wrong == 0)
                        firstMismatch = "(" + std::to_string(x) + "," + std::to_string(y) +
                                        ") got " + ColorText(got);
                    ++wrong;
                }
            }
        check(untouched == 0, "F10: every element of the whole-level overload was written");
        if (wrong != 0) std::printf("        first mismatch %s\n", firstMismatch.c_str());
        if (kGeneratedMipContentAsserted)
            check(wrong == 0, "F10: the whole-level overload still returns level 0 exactly");
        GuardsIntact(buf, count, "F10");
    }

    // ================================================================ Group G: lifetime

    /** @brief G1 -- dispose an uneven target immediately after its final-level read. */
    void LegG1()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = MakeTarget(dev, 13, 7, true, 4, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, "G1 13x7");
        if (!rt) return;
        RenderPattern(dev, *rt);
        ReadWholeLevel(*rt, rt->getLevelCountProperty() - 1,
                       "G1 13x7 final level before disposal");
        step("G1: Dispose() immediately after the read");
        rt->Dispose();
        rt.reset();
        check(true, "G1: disposing the uneven target straight after its final-level read is clean");
    }

    /** @brief G2 -- hold an uneven target to device teardown after its final-level read. */
    void LegG2()
    {
        auto& dev = getGraphicsDeviceProperty();
        heldToTeardown_ = MakeTarget(dev, 13, 7, true, 4, DepthFormat::None,
                                     RenderTargetUsage::DiscardContents, "G2 13x7");
        if (!heldToTeardown_) return;
        RenderPattern(dev, *heldToTeardown_);
        ReadWholeLevel(*heldToTeardown_, heldToTeardown_->getLevelCountProperty() - 1,
                       "G2 13x7 final level, target then held to teardown");
        check(true, "G2: the uneven target survives to GraphicsDevice teardown after its "
                    "final-level read");
    }

    /** @brief G3 -- six create / render / read-level-1 / dispose cycles in one process. */
    void LegG3()
    {
        auto& dev = getGraphicsDeviceProperty();
        for (int i = 0; i < 6; ++i)
        {
            const std::string label = "G3 cycle " + std::to_string(i);
            auto rt = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                                 RenderTargetUsage::DiscardContents, label);
            if (!rt) return;
            RenderPattern(dev, *rt);
            ReadWholeLevel(*rt, 1, label + " level 1");
            rt->Dispose();
        }
    }

    /** @brief G4 -- A, B, then A again: uneven targets must regenerate their complete chains. */
    void LegG4()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto a = MakeTarget(dev, 13, 7, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "G4 target A 13x7");
        if (!a) return;
        auto b = MakeTarget(dev, 8, 4, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "G4 target B 8x4");
        if (!b) return;
        RenderPattern(dev, *a);
        RenderPattern(dev, *b);
        RenderPattern(dev, *a);   // A -> B -> A: A's chain must reflect the LAST render into it
        for (int level = 0; level < a->getLevelCountProperty(); ++level)
            ReadWholeLevel(*a, level, "G4 A level " + std::to_string(level) + " after A->B->A");
        for (int level = 0; level < b->getLevelCountProperty(); ++level)
            ReadWholeLevel(*b, level, "G4 B level " + std::to_string(level) + " after A->B->A");
    }

    /** @brief G5 -- an exception thrown by a refused read must not damage the next real one. */
    void LegG5()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto rt = ShapeTarget(dev, "G5");
        if (!rt) return;
        ExpectLevelRejected(*rt, rt->getLevelCountProperty() + 3, "G5 out-of-range level");
        ReadWholeLevel(*rt, 1, "G5 level 1 after the exception");
        ReadWholeLevel(*rt, 2, "G5 level 2 after the exception");
    }

    // ================================================================ Group H: the chain matrix

    /**
     * @brief Every level of one chain: dimensions, existence, exact-or-bounded content, rejection.
     *
     * Small and degenerate shapes go through exactly the same public contract as the canonical
     * target: no level may crash, every level must be written in full, no level may be
     * (0,0,0,0), and `level == LevelCount` must reject.
     */
    void ChainLeg(const char* id, int w, int h, int samples = 4)
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::string label = std::string(id) + " " + std::to_string(w) + "x" + std::to_string(h) +
                                  " MSAA=" + std::to_string(samples);
        auto rt = MakeTarget(dev, w, h, true, samples, DepthFormat::None,
                             RenderTargetUsage::DiscardContents, label);
        if (!rt) return;
        AssertChain(*rt, kMipMappedTargetSupported, label);
        RenderPattern(dev, *rt);
        const int levels = rt->getLevelCountProperty();
        for (int level = 0; level < levels; ++level)
        {
            const int lw = LevelDim(w, level);
            const int lh = LevelDim(h, level);
            note(label + " level " + std::to_string(level) + " expected dimensions " +
                 std::to_string(lw) + "x" + std::to_string(lh));
            ReadWholeLevel(*rt, level, label + " level " + std::to_string(level));
        }
        ExpectLevelRejected(*rt, levels, label + " level == LevelCount");
    }

    void LegH1()  { ChainLeg("H1",  1,  1); }
    void LegH2()  { ChainLeg("H2",  2,  2); }
    void LegH3()  { ChainLeg("H3",  3,  2); }
    void LegH4()  { ChainLeg("H4",  5,  3); }
    void LegH5()  { ChainLeg("H5",  8,  4); }
    void LegH6()  { ChainLeg("H6", 13,  7); }

    // The same three UNEVEN chains without multisampling at all. REMED-GFX-187 is a property of
    // mip-generation arithmetic, not resolve, so matching H4/H5/H6 proves the corrected contract
    // on both routes while keeping REMED-GFX-186's resolve subject independently covered.
    void LegH7()  { ChainLeg("H7",  5,  3, 0); }
    void LegH8()  { ChainLeg("H8",  8,  4, 0); }
    void LegH9()  { ChainLeg("H9", 13,  7, 0); }
    // REMED-GFX-193: `mipMap=true` means the FNA-calculated chain, not "there must be a
    // generated descendant".  A 1x1 target therefore has one public/native level whether or
    // not MSAA applies.  H1 above is the MSAA route; this is its non-MSAA counterpart.
    void LegH10() { ChainLeg("H10", 1, 1, 0); }

    // REMED-GFX-187: the axes that reach one first are covered in BOTH orientations and on both
    // the real-MSAA-resolve and non-MSAA routes. Every ChainLeg reads every intermediate level,
    // the final 1x1 level, and then proves that LevelCount itself is not a fabricated level.
    void LegH11() { ChainLeg("H11",  1, 13, 4); }
    void LegH12() { ChainLeg("H12", 13,  1, 4); }
    void LegH13() { ChainLeg("H13",  2, 13, 4); }
    void LegH14() { ChainLeg("H14", 13,  2, 4); }
    void LegH15() { ChainLeg("H15",  1, 13, 0); }
    void LegH16() { ChainLeg("H16", 13,  1, 0); }
    void LegH17() { ChainLeg("H17",  2, 13, 0); }
    void LegH18() { ChainLeg("H18", 13,  2, 0); }
    void LegH19() { ChainLeg("H19",  3,  2, 0); }

    // ================================================================ Group I: other routes

    /**
     * @brief I1 -- an MRT member: the SAME helper, so the fix must cover it and this proves it.
     *
     * `SetRenderTargets({a, b})` makes `b` a real simultaneous colour attachment of ONE pass, but
     * `b` is still an ordinary `RenderTarget2D` read through `SdlGpuRenderTargetRenderer::GetData`.
     * Its generated chain is therefore the same route, not a separate one -- which is exactly why
     * it belongs in this ticket rather than in a new one.
     */
    void LegI1()
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!kMrtSupported)
        {
            boundary("I1 multiple simultaneous render targets are a declared boundary here");
            check(true, "I1 the MRT boundary is declared rather than assumed");
            return;
        }
        auto a = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "I1 MRT attachment 0");
        if (!a) return;
        auto b = MakeTarget(dev, kRT, kRT, true, 4, DepthFormat::None,
                            RenderTargetUsage::DiscardContents, "I1 MRT attachment 1");
        if (!b) return;

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        step("I1: SetRenderTargets({a, b}), Clear, three quadrant quads, unbind");
        std::vector<RenderTargetBinding> bindings;
        bindings.emplace_back(a.get());
        bindings.emplace_back(b.get());
        dev.SetRenderTargets(bindings);
        dev.Clear(kBase);
        DrawFlatQuad(dev, kTL, -1.f,  0.f,  0.f,  1.f);
        DrawFlatQuad(dev, kTR,  0.f,  0.f,  1.f,  1.f);
        DrawFlatQuad(dev, kBL, -1.f, -1.f,  0.f,  0.f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        ReadWholeLevel(*a, 0, "I1 MRT attachment 0 level 0");
        ReadWholeLevel(*a, 1, "I1 MRT attachment 0 level 1");
        // Stock single-output draws write attachment 0 only (SDLGPU-37), so attachment 1 holds the
        // pass' CLEAR colour. Its LEVEL 1 must still be that colour rather than a crash, an
        // ungenerated zero, or a copy of attachment 0.
        std::vector<Color> extra = ReadWholeLevel(*b, 1, "I1 MRT attachment 1 level 1",
                                                  /*contentAsserted=*/false);
        if (kTargetReadbackSupported)
        {
            note("I1 MRT attachment 1 level 1 first texel = " + ColorText(extra.front()));
            if (kMrtExtraAttachmentContentClaimable)
            {
                bool allBase = true;
                for (const Color& c : extra)
                    if (!Near(c, kBase)) { allBase = false; break; }
                check(allBase, "I1: the extra MRT attachment's generated level 1 holds the pass' "
                               "clear colour -- neither ungenerated nor a copy of attachment 0");
            }
            else
            {
                boundary("I1 an MRT output a stock single-output draw never wrote is UNDEFINED in "
                         "XNA, so its generated level is measured and printed here rather than "
                         "claimed; the read itself is fully asserted above");
            }
        }
    }

    /**
     * @brief I2 -- a mipmapped MULTISAMPLED RenderTargetCube, re-armed for REMED-GFX-188.
     *
     * GFX-186 originally classified the SDL_GPU cube as a separate, safely refusing route. GFX-188
     * now requires its resolved cube to own and populate every public level, so the SDL_GPU branch
     * below must answer and expose the generated level-1 content. Other renderers retain this
     * fixture's original all-or-nothing safety boundary; the focused GFX-188 matrix is SDL_GPU-only.
     */
    void LegI2()
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!kCubeTargetSupported)
        {
            boundary("I2 RenderTargetCube is a declared boundary here");
            check(true, "I2 the cube boundary is declared rather than assumed");
            return;
        }
        step("I2: RenderTargetCube(16, mipMap=true, MSAA=4) -- cube mip route");
        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(dev, kRT, true, SurfaceFormat::Color,
                                                      DepthFormat::None, 4,
                                                      RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            boundary(std::string("I2 a mipmapped multisampled cube target is refused here: ") + e.what());
            check(true, "I2: the refusal is a catchable public exception, not an abort");
            return;
        }
        note("I2 cube LevelCount=" + std::to_string(cube->getLevelCountProperty()) +
             " appliedSamples=" + std::to_string(cube->getMultiSampleCountProperty()));

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        dev.Clear(kBase);
        DrawFlatQuad(dev, kTL, -1.f, 0.f, 0.f, 1.f);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const int count = (kRT / 2) * (kRT / 2);
        std::vector<Color> buf = SentinelBuffer(count);
        const Rectangle r(0, 0, kRT / 2, kRT / 2);
        step("I2: RenderTargetCube::GetData(PositiveX, level=1, ...)");
        bool threw = false;
        std::string what;
        try { cube->GetData(CubeMapFace::PositiveX, 1, &r, buf.data(), kGuard, count); }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        int untouched = 0, zeroed = 0;
        const Color black(0, 0, 0, 0);
        for (int i = 0; i < count; ++i)
        {
            const Color c = buf[static_cast<std::size_t>(kGuard + i)];
            if (Exact(c, kSentinel)) ++untouched;
            if (Exact(c, black)) ++zeroed;
        }
        boundary(std::string("I2 mipmapped multisampled CUBE level 1 on this renderer: ") +
                 (threw ? ("REFUSED -- " + what) : "answered") + "; " + std::to_string(untouched) +
                 "/" + std::to_string(count) + " destination elements untouched, " +
                 std::to_string(zeroed) + "/" + std::to_string(count) + " exactly (0,0,0,0)");
#if defined(CNA_RENDERER_SDL_GPU)
        check(!threw, "I2: SDL_GPU answers the valid multisampled cube level-1 read");
        check(untouched == 0,
              "I2: SDL_GPU writes the complete level-1 destination window");
        check(zeroed == 0,
              "I2: SDL_GPU's generated cube level contains no fabricated zero texels");
        if (!threw)
        {
            check(Near(buf[static_cast<std::size_t>(kGuard + 1 * (kRT / 2) + 1)], kTL),
                  "I2: SDL_GPU level 1 retains the rendered top-left colour");
            check(Near(buf[static_cast<std::size_t>(kGuard + 6 * (kRT / 2) + 6)], kBase),
                  "I2: SDL_GPU level 1 retains the resolved clear colour away from the draw");
        }
#else
        check(true, "I2: the cube route completed without a signal");
        check(threw ? untouched == count : untouched == 0,
              "I2: the cube route either wrote its whole window or wrote nothing at all -- never "
              "a partially filled destination");
#endif
        GuardsIntact(buf, count, "I2");
    }

    /** @brief Samples the final 1x1 mip of a 13x7 target after two independent generations. */
    void SampleFinalMipLeg(const char* id, int samples)
    {
        auto& dev = getGraphicsDeviceProperty();
        const std::string label = std::string(id) + " 13x7 MSAA=" + std::to_string(samples);
        auto source = MakeTarget(dev, 13, 7, true, samples, DepthFormat::None,
                                 RenderTargetUsage::DiscardContents, label + " source");
        if (!source) return;
        auto sampled = MakeTarget(dev, 1, 1, false, 0, DepthFormat::None,
                                  RenderTargetUsage::DiscardContents, label + " destination");
        if (!sampled) return;

        SpriteBatch batch(dev);
        SamplerState linear = SamplerState::LinearClamp;
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            const std::string cycleLabel = label + " cycle " + std::to_string(cycle);
            RenderPattern(dev, *source);

            step(cycleLabel + ": sample the whole 13x7 source into a 1x1 destination");
            dev.SetRenderTarget(sampled.get());
            dev.Clear(kSentinel);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linear, nullptr, nullptr);
            batch.Draw(*source, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 13, 7), Color::White);
            batch.End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const int finalLevel = source->getLevelCountProperty() - 1;
            const std::vector<Color> final =
                ReadWholeLevel(*source, finalLevel, cycleLabel + " direct final-level read");

            std::vector<Color> sample = SentinelBuffer(1);
            const Rectangle one(0, 0, 1, 1);
            step(cycleLabel + ": read the 1x1 sampling destination");
            if (!IssueRead([&] { sampled->GetData(0, &one, sample.data(), kGuard, 1); },
                           sample, cycleLabel + " sampled pixel"))
                continue;
            check(!Exact(sample[static_cast<std::size_t>(kGuard)], kSentinel),
                  cycleLabel + ": sampling overwrote the destination's sentinel");
            GuardsIntact(sample, 1, cycleLabel + " sampled pixel");

            if (kTargetMipReadbackSupported)
            {
                const Color got = sample[static_cast<std::size_t>(kGuard)];
                check(Near(got, final.front()),
                      cycleLabel + ": extreme minification samples the declared final 1x1 mip "
                          "(got " + ColorText(got) + ", direct final " +
                          ColorText(final.front()) + ")");
            }
            else
            {
                boundary(cycleLabel + ": this renderer refuses nonzero target-mip readback, so the "
                         "sampled pixel is measured but cannot be compared to a direct final mip");
                check(true, cycleLabel + ": sampling completed without inventing a direct-mip "
                            "claim on this renderer");
            }
        }

        source->Dispose();
        sampled->Dispose();
        check(true, label + ": repeated generation, sampling, readback and disposal completed");
    }

    void LegI3() { SampleFinalMipLeg("I3", 4); }
    void LegI4() { SampleFinalMipLeg("I4", 0); }

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
        runLeg("A1", &RenderTargetMsaaMipReadbackTest::LegA1);
        runLeg("A2", &RenderTargetMsaaMipReadbackTest::LegA2);
        runLeg("A3", &RenderTargetMsaaMipReadbackTest::LegA3);
        runLeg("A4", &RenderTargetMsaaMipReadbackTest::LegA4);
        runLeg("A5", &RenderTargetMsaaMipReadbackTest::LegA5);
        runLeg("A6", &RenderTargetMsaaMipReadbackTest::LegA6);
        runLeg("A7", &RenderTargetMsaaMipReadbackTest::LegA7);
        runLeg("A8", &RenderTargetMsaaMipReadbackTest::LegA8);

        runLeg("B0",  &RenderTargetMsaaMipReadbackTest::LegB0);
        runLeg("B1",  &RenderTargetMsaaMipReadbackTest::LegB1);
        runLeg("B2",  &RenderTargetMsaaMipReadbackTest::LegB2);
        runLeg("B4",  &RenderTargetMsaaMipReadbackTest::LegB4);
        runLeg("B8",  &RenderTargetMsaaMipReadbackTest::LegB8);
        runLeg("B16", &RenderTargetMsaaMipReadbackTest::LegB16);
        runLeg("B64", &RenderTargetMsaaMipReadbackTest::LegB64);

        runLeg("C1", &RenderTargetMsaaMipReadbackTest::LegC1);
        runLeg("C2", &RenderTargetMsaaMipReadbackTest::LegC2);
        runLeg("C3", &RenderTargetMsaaMipReadbackTest::LegC3);

        runLeg("D1", &RenderTargetMsaaMipReadbackTest::LegD1);
        runLeg("D2", &RenderTargetMsaaMipReadbackTest::LegD2);
        runLeg("D3", &RenderTargetMsaaMipReadbackTest::LegD3);
        runLeg("D4", &RenderTargetMsaaMipReadbackTest::LegD4);

        runLeg("E1", &RenderTargetMsaaMipReadbackTest::LegE1);
        runLeg("E2", &RenderTargetMsaaMipReadbackTest::LegE2);
        runLeg("E3", &RenderTargetMsaaMipReadbackTest::LegE3);

        runLeg("F1",  &RenderTargetMsaaMipReadbackTest::LegF1);
        runLeg("F2",  &RenderTargetMsaaMipReadbackTest::LegF2);
        runLeg("F3",  &RenderTargetMsaaMipReadbackTest::LegF3);
        runLeg("F4",  &RenderTargetMsaaMipReadbackTest::LegF4);
        runLeg("F5",  &RenderTargetMsaaMipReadbackTest::LegF5);
        runLeg("F6",  &RenderTargetMsaaMipReadbackTest::LegF6);
        runLeg("F7",  &RenderTargetMsaaMipReadbackTest::LegF7);
        runLeg("F8",  &RenderTargetMsaaMipReadbackTest::LegF8);
        runLeg("F9",  &RenderTargetMsaaMipReadbackTest::LegF9);
        runLeg("F10", &RenderTargetMsaaMipReadbackTest::LegF10);

        runLeg("G1", &RenderTargetMsaaMipReadbackTest::LegG1);
        runLeg("G2", &RenderTargetMsaaMipReadbackTest::LegG2);
        runLeg("G3", &RenderTargetMsaaMipReadbackTest::LegG3);
        runLeg("G4", &RenderTargetMsaaMipReadbackTest::LegG4);
        runLeg("G5", &RenderTargetMsaaMipReadbackTest::LegG5);

        runLeg("H1", &RenderTargetMsaaMipReadbackTest::LegH1);
        runLeg("H2", &RenderTargetMsaaMipReadbackTest::LegH2);
        runLeg("H3", &RenderTargetMsaaMipReadbackTest::LegH3);
        runLeg("H4", &RenderTargetMsaaMipReadbackTest::LegH4);
        runLeg("H5", &RenderTargetMsaaMipReadbackTest::LegH5);
        runLeg("H6", &RenderTargetMsaaMipReadbackTest::LegH6);
        runLeg("H7", &RenderTargetMsaaMipReadbackTest::LegH7);
        runLeg("H8", &RenderTargetMsaaMipReadbackTest::LegH8);
        runLeg("H9", &RenderTargetMsaaMipReadbackTest::LegH9);
        runLeg("H10", &RenderTargetMsaaMipReadbackTest::LegH10);
        runLeg("H11", &RenderTargetMsaaMipReadbackTest::LegH11);
        runLeg("H12", &RenderTargetMsaaMipReadbackTest::LegH12);
        runLeg("H13", &RenderTargetMsaaMipReadbackTest::LegH13);
        runLeg("H14", &RenderTargetMsaaMipReadbackTest::LegH14);
        runLeg("H15", &RenderTargetMsaaMipReadbackTest::LegH15);
        runLeg("H16", &RenderTargetMsaaMipReadbackTest::LegH16);
        runLeg("H17", &RenderTargetMsaaMipReadbackTest::LegH17);
        runLeg("H18", &RenderTargetMsaaMipReadbackTest::LegH18);
        runLeg("H19", &RenderTargetMsaaMipReadbackTest::LegH19);

        runLeg("I1", &RenderTargetMsaaMipReadbackTest::LegI1);
        runLeg("I2", &RenderTargetMsaaMipReadbackTest::LegI2);
        runLeg("I3", &RenderTargetMsaaMipReadbackTest::LegI3);
        runLeg("I4", &RenderTargetMsaaMipReadbackTest::LegI4);

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
    explicit RenderTargetMsaaMipReadbackTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
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
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8",
        "B0", "B1", "B2", "B4", "B8", "B16", "B64",
        "C1", "C2", "C3",
        "D1", "D2", "D3", "D4",
        "E1", "E2", "E3",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
        "G1", "G2", "G3", "G4", "G5",
        "H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9", "H10",
        "H11", "H12", "H13", "H14", "H15", "H16", "H17", "H18", "H19",
        "I1", "I2", "I3", "I4",
    };

#if CNA_GFX186_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever.
    constexpr unsigned kLegTimeoutSeconds = 180;

    /**
     * @brief Runs one leg in a child process and classifies the outcome exactly.
     *
     * The defect this file reproduces is an UNCATCHABLE SIGSEGV, so containment is the only way the
     * remaining legs can produce results at all -- and "the FIRST mip read of the process" is a
     * property this file asserts, which only a fresh process can guarantee.
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
                std::printf("[FATAL] leg %s: SIGSEGV%s -- REMED-GFX-186's signature; the last "
                            "[STEP] line names the public call that reached it\n", legId, core);
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

#if CNA_GFX186_CAN_FORK
    if (onlyLeg.empty())
    {
        const int total = static_cast<int>(sizeof(kLegs) / sizeof(kLegs[0]));
        std::printf("[INFO] REMED-GFX-186 supervisor on %s: %d legs, each in its own process\n",
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

    RenderTargetMsaaMipReadbackTest game(std::move(onlyLeg));
    game.Run();
    return game.Result();
}
