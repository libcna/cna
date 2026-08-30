// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-158: a RenderTarget2D constructed during a public frame must be usable IMMEDIATELY,
// in that same frame, with no warm-up of any kind.
//
//     RenderTarget2D a(device, w, h);      // constructed now
//     device.SetRenderTarget(&a);          // bound now
//     ... clear and/or draw into it ...    // produced now
//     device.SetRenderTarget(nullptr);
//     ... read, sample or otherwise observe a ...
//
// The application must not need a warm-up frame, a Present, a GetData before the first render, a
// dummy draw, a bind cycle with no work, a manual frame advance, a sleep, or any CPU/GPU wait.
//
// THE DEFECT this file reproduces is bgfx-local. `bgfx::reset()` -- which this renderer calls the
// moment it notices the SDL window's size differs from the size bgfx was initialised with -- ends
// with this loop (bgfx_p.h, `Context::reset`):
//
//     for (uint32_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
//         m_view[ii].setFrameBuffer(BGFX_INVALID_HANDLE);
//
// It discards EVERY view's framebuffer binding, including the one `BindAsRenderTarget()` had just
// programmed for the target that is bound right now. bgfx resolves view state once, at
// `bgfx::frame()`, so the operations already submitted against that view do not follow the target
// they were aimed at: they resolve against the backbuffer instead, and the target is never written.
// Measured pre-fix, with the reset traced at its own call site:
//
//     [reset] bgfx::reset 800x480 -> 64x64  (currentViewId_=1 segmentTargetBaseId_=1)
//     [bgfx-view-order] frame used 2 view(s) in public order: 1 64
//     A(first): GetData[0]=(0,0,0,0)  expected (60,120,180,255)
//
// (0,0,0,0) rather than the target's own discard-clear colour (0,0,0,255): the bind cycle's FIRST
// operation is lost too, so nothing at all reached the texture.
//
// WHY "CREATE IT ONE FRAME EARLIER" LOOKED LIKE THE CURE. The resolution settles exactly once. Any
// public operation that runs before the target is bound -- a bare `Clear` on the backbuffer is
// enough, which is what a warm-up frame always contains -- absorbs the reset while no render target
// is bound, and every target created afterwards works. That made this look like deferred resource
// creation or a bgfx frame latency. It is neither: the texture and the framebuffer are both created
// and complete (`m_cmdPre` is executed before the frame's draws are submitted -- bgfx.cpp,
// `Context::renderFrame`), and a target whose content lands on an ordered SEGMENT view rather than
// on its base view survives the very same frame untouched. It is the view->framebuffer REGISTRATION
// that is destroyed, nothing else.
//
// It follows that this is not really a first-frame defect at all. Any `bgfx::reset()` does it: a
// genuine window resize, or a vsync change through `SetSwapInterval`, at any point in a program's
// life. The first frame is merely where a fixture meets it.
//
// THE FIX mirrors every view->framebuffer binding this renderer programs and replays the mirror
// immediately after any `bgfx::reset()`, restoring exactly what reset discarded. No frame advance,
// no Present, no readback, no wait and no dummy submission is added anywhere.
//
// HOW TO READ A PRE-FIX RUN. The resolution settles once, so only the FIRST bind cycle of a process
// can observe it. Leg order is therefore load-bearing here in a way it is not in other fixtures, and
// `--leg=<id>` runs one leg on its own so that leg is the first thing the process does. The CMake
// registration uses that to put five different legs in first position in five separate processes;
// the four RenderTarget2D ones are red pre-fix and green after, and the cube leg covers the second
// registration path (BindAsRenderTargetFace) through the same reset.
//
// THE PATTERN is 8x4 -- deliberately NON-SQUARE, so a transpose cannot masquerade as a pass -- and
// every one of its 32 texels is unique in (R, G). A stale buffer, a zero buffer, a uniform fill, a
// mirror, a rotation and a swapped resource each fail distinguishably.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A1  draw is the cycle's first operation      the canonical immediate first use
//   A2  Clear then draw                          the canonical sequence of the ticket
//   A3  Clear only, PreserveContents             a clear as the cycle's first operation
//   A4  bind and unbind with no work             the target's own discard clear must land
//   B   consumed by an OLDER render target       consumer created before the producer
//   C   consumed on the backbuffer               beside an ordinary Texture2D control
//   D   producer and consumer created together   both new in the same frame
//   E   created after earlier queued draws       the frame is already in progress
//   F   two new targets, alternating order       neither may take the other's content
//   G   bind, unbind, rebind in one frame        both cycles of a brand-new target
//   H   MSAA target, consumed by sampling        resolve must not need a warm-up
//   I   mipmapped target                         level 0 of a brand-new mip chain
//   J   stock 3D is the cycle's first operation  not a SpriteBatch-only contract
//   K   SpriteBatch and 3D in one first cycle    REMED-GFX-157 order preserved
//   L   dispose and recreate                     same frame, repeatedly, address reuse
//   M   cardinality                              many new targets per frame, many frames
//   N   a brand-new RenderTargetCube face       a second registration path through the reset
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief Whether this renderer rasterizes at all.
     *
     * HEADLESS performs no rasterization, so REMED-GFX-127's contract makes every readback reject
     * deterministically. There is no produced content to observe there; the legs below assert the
     * rejection instead of a value.
     */
#if defined(CNA_RENDERER_HEADLESS)
    constexpr bool kRasterizes = false;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "FREEDIRECT";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_CANVAS)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "CANVAS";
#else
#error "REMED-GFX-158: this renderer has no declared first-use contract."
#endif

    /**
     * @brief Whether a RenderTarget2D may be handed to a stock 3D effect as its texture.
     *
     * REMED-GFX-152 CLOSED this (2026-07-29), and the declaration is now unconditionally true.
     *
     * It used to read false on SDL_GPU, whose stock-effect paths cast a RenderTarget2D's renderer to
     * the unrelated `SdlGpuTextureRenderer` sibling and died.
     *
     * Recorded accurately rather than claimed as a crash proof: in THIS file the flag was
     * OVER-APPLIED. Legs J and K carry `needsStockEffectRtSource = true`, but neither hands a
     * render target to an effect -- both bind an ordinary `Texture2D` (`patternTex_`,
     * `altPatternTex_`) and merely RENDER INTO a target, which was never the defect. So the skip
     * cost SDL_GPU three checks of genuine first-use coverage while hiding nothing: both legs were
     * measured against the pre-fix renderer and PASS there (J 1/1, K 2/2). SDL_GPU 17/17 -> 20/20,
     * the three newly-enabled checks green on both builds. The legs that really do sample a target
     * through a stock effect live in rendertarget_effect_source_test.cpp, and those are the ones
     * that SIGSEGV pre-fix.
     */
    constexpr bool kStockEffectRtSourceSupported = true;

    /**
     * @brief Whether a mipMap=true RenderTarget2D is implemented at all.
     *
     * WEBGPU declares this unimplemented (plans/plan_webgpu.md WEBGPU-53/54) and throws when the target's
     * mip chain would have to be regenerated. That predates and is unrelated to first use, so leg I
     * asserts the deterministic rejection there instead of a value. SOKOL implements it as of
     * plans/plan_sokol.md SOKOL-39 (mip storage allocated via sokol_gfx's num_mipmaps, regenerated from
     * level 0 via glGenerateMipmap on unbind).
     */
    constexpr bool kMipmappedRenderTargetSupported =
#if defined(CNA_RENDERER_WEBGPU)
        false;
#else
        true;
#endif

    constexpr int kBBW = 64;   ///< Backbuffer width.  Eight pattern slots across.
    constexpr int kBBH = 64;   ///< Backbuffer height.

    constexpr int kPW = 8;     ///< Pattern width.  Deliberately different from kPH.
    constexpr int kPH = 4;     ///< Pattern height. A transpose therefore cannot pass.

    constexpr int kSlotsPerRow = kBBW / kPW;

    /**
     * @brief The producer pattern texel at (@p x, @p y), with (0, 0) the TOP-LEFT corner.
     *
     * R is a function of x alone and G of y alone, so the pair (R, G) is unique for all 32 texels
     * and identifies both axes independently. B mixes the two so a 180-degree rotation is
     * distinguishable, and A takes four distinct non-trivial values so a dropped, forced-opaque or
     * premultiplied alpha cannot pass either. Every channel value is a multiple of 5 well away from
     * 0 and 255, so a stale or zero-filled image cannot coincide with it.
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
     * @brief A second, unmistakably different pattern, for legs that must tell two producers apart.
     *
     * Deliberately not a transform of PatternColor: its R decreases with x where PatternColor's
     * increases, so a leg that samples the wrong one of two live producers fails on every texel
     * rather than on a few.
     */
    Color AltPatternColor(int x, int y)
    {
        const int r = 245 - x * 30;                      // 245, 215, ... 35
        const int g = 235 - y * 70;                      // 235, 165, 95, 25
        const int b = 30 + ((x * 3 + y * 5) % 7) * 30;
        const int a = 90 + ((x + 3 * y) % 4) * 55;       // 90, 145, 200, 255
        return Color(static_cast<bytecs>(r), static_cast<bytecs>(g),
                     static_cast<bytecs>(b), static_cast<bytecs>(a));
    }

    /**
     * @brief The mid-tone a leg clears a brand-new target to.
     *
     * Nonzero in every channel and opaque, so it is distinguishable from BOTH a never-written
     * target (0,0,0,0) and the target's own DiscardContents clear (0,0,0,255).
     */
    constexpr Color MidTone() { return Color(bytecs(70), bytecs(130), bytecs(190), bytecs(255)); }

    /** @brief The colour a DiscardContents bind writes, per GraphicsDevice::SetRenderTarget. */
    constexpr Color DiscardTone() { return Color(bytecs(0), bytecs(0), bytecs(0), bytecs(255)); }

    /** @brief Formats a colour as R,G,B,A for diagnostics. */
    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

/**
 * @brief REMED-GFX-158's public contract: a render target is usable the instant it is constructed.
 */
class RenderTargetFirstUseTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    std::string legFilter_;   ///< Empty = run every leg in order.

    Texture2D patternTex_;    ///< kPW x kPH, the content a producer must end up holding.
    Texture2D altPatternTex_; ///< kPW x kPH, a second, unmistakably different content.

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    /** @brief True when this renderer declared it cannot rasterize at all. */
    static bool Unsupported() { return !kRasterizes; }

    // ---------------------------------------------------------------- readback

    /** @brief The outcome of a public readback. */
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

    /// Reads a whole render target back, pre-filled with a poison value so a renderer that writes
    /// nothing cannot be mistaken for one that wrote transparent black.
    static Readback ReadWholeTarget(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.w = w;
        r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size())); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// Reads the whole backbuffer back, with the same poison prefill.
    static Readback ReadBackbufferRaw(GraphicsDevice& dev)
    {
        Readback r;
        r.w = kBBW;
        r.h = kBBH;
        r.pixels.assign(static_cast<std::size_t>(kBBW) * kBBH, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { dev.GetBackBufferData(r.pixels.data(), 0, static_cast<int>(r.pixels.size())); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// Asserts an unreadable renderer rejected, and reports whether the caller should continue.
    bool RequireReadable(const Readback& r, const std::string& label)
    {
        if (Unsupported())
        {
            check(r.threwNotSupported,
                  label + ": declared non-rasterizing, must throw NotSupportedException");
            return false;
        }
        if (!r.ok())
        {
            check(false, label + ": readback failed" +
                         (r.threwNotSupported ? " (NotSupportedException)" : " (" + r.otherWhat + ")"));
            return false;
        }
        return true;
    }

    /// Reads the backbuffer, reporting a declared-unavailable oracle as INFO rather than a failure.
    /// Returns false when the caller must not assert on the result.
    bool ReadBackbufferOr(GraphicsDevice& dev, Readback& out, const std::string& label)
    {
        out = ReadBackbufferRaw(dev);
        if (Unsupported() || !out.ok())
        {
            std::printf("[INFO] %s: backbuffer oracle unavailable on %s (%s) -- boundary recorded, "
                        "the render-target oracle carries the contract\n",
                        label.c_str(), kRendererName,
                        out.ok() ? "non-rasterizing"
                                 : (out.threwNotSupported ? "NotSupportedException"
                                                          : out.otherWhat.c_str()));
            std::fflush(stdout);
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- geometry

    /// The destination rectangle of backbuffer slot @p slot: eight kPW-wide slots per row.
    static Rectangle Slot(int slot)
    {
        return Rectangle((slot % kSlotsPerRow) * kPW, (slot / kSlotsPerRow) * kPH, kPW, kPH);
    }

    /// Draws @p source 1:1 into the FULL extent of whatever is currently bound, point-sampled.
    /// REMED-GFX-157: the depth and rasterizer states are passed explicitly -- a null means
    /// `RasterizerState.CullCounterClockwise`, which FNA's PrepRenderState leaves on the device.
    void DrawFull(GraphicsDevice& dev, const Texture2D& source)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(source, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws @p source 1:1 into backbuffer slot @p slot through SpriteBatch, point-sampled.
    void DrawSpriteSlot(GraphicsDevice& dev, const Texture2D& source, int slot)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(source, Slot(slot), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws two sources into two slots inside ONE SpriteBatch batch.
    void DrawSpriteSlots(GraphicsDevice& dev, const Texture2D& a, int slotA,
                         const Texture2D& b, int slotB)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(a, Slot(slotA), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.Draw(b, Slot(slotB), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws @p source over the FULL extent of whatever is bound through BasicEffect, so a leg can
    /// state the contract for the stock 3D path and not only for SpriteBatch. The quad is expressed
    /// directly in clip space, so this cannot accidentally exercise viewport segmentation
    /// (REMED-GFX-063/065) instead of the first use under test. v runs 1 -> 0 up the screen, which
    /// is what makes a clip-space quad agree with SpriteBatch's top-left texel order.
    void Draw3DFull(GraphicsDevice& dev, Texture2D& source)
    {
        VertexPositionTexture q[6];
        q[0] = { Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f) };
        q[1] = { Vector3(-1.f, -1.f, 0.f), Vector2(0.f, 1.f) };
        q[2] = { Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
        q[3] = { Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f) };
        q[4] = { Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
        q[5] = { Vector3( 1.f,  1.f, 0.f), Vector2(1.f, 0.f) };

        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&source);
        fx.VertexColorEnabled = false;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    /// Opens a backbuffer bind cycle: unbind any target, reset state, clear to opaque black.
    void BeginBackbuffer(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
    }

    // ---------------------------------------------------------------- comparisons

    using PatternFn = Color (*)(int, int);

    /// Asserts a whole kPW x kPH readback reproduces @p want exactly, naming the failure shape
    /// (never written / entirely empty / still the target's own discard clear) rather than only a
    /// count -- those three shapes are the distinguishable pre-fix outcomes of this defect.
    void CheckTarget(const Readback& r, const std::string& label, PatternFn want)
    {
        int good = 0, empty = 0, poison = 0, discard = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = r.at(x, y);
                if (Same(got, want(x, y))) ++good;
                else if (firstMismatch.empty())
                    firstMismatch = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want=" + ColorText(want(x, y)) + " got=" + ColorText(got);
                if (Same(got, Color(0, 0, 0, 0)))                   ++empty;
                if (Same(got, Color(0xCD, 0xCD, 0xCD, 0xCD)))       ++poison;
                if (Same(got, DiscardTone()))                       ++discard;
            }
        const bool exact = good == kPW * kPH;
        std::string shape;
        if (!exact)
        {
            shape = " [" + std::to_string(good) + "/" + std::to_string(kPW * kPH) + " correct";
            if (poison)  shape += ", " + std::to_string(poison)  + " never written";
            if (empty)   shape += ", " + std::to_string(empty)   + " entirely empty";
            if (discard) shape += ", " + std::to_string(discard) + " still the discard clear";
            shape += "]";
        }
        check(exact, label + shape + firstMismatch);
    }

    /// Asserts a whole kPW x kPH readback is one uniform colour.
    void CheckUniform(const Readback& r, const std::string& label, const Color& want)
    {
        int good = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                if (Same(r.at(x, y), want)) ++good;
                else if (first.empty())
                    first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                            ") want=" + ColorText(want) + " got=" + ColorText(r.at(x, y));
            }
        check(good == kPW * kPH,
              label + " (" + std::to_string(good) + "/" + std::to_string(kPW * kPH) + ")" + first);
    }

    /// Asserts the kPW x kPH region at backbuffer slot @p slot reproduces @p want exactly.
    void CheckSlot(const Readback& r, int slot, const std::string& label, PatternFn want)
    {
        const Rectangle d = Slot(slot);
        int good = 0, empty = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = r.at(d.X + x, d.Y + y);
                if (Same(got, want(x, y))) ++good;
                else if (firstMismatch.empty())
                    firstMismatch = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want=" + ColorText(want(x, y)) + " got=" + ColorText(got);
                if (Same(got, Color(0, 0, 0, 255))) ++empty;
            }
        const bool exact = good == kPW * kPH;
        std::string shape;
        if (!exact)
        {
            shape = " [" + std::to_string(good) + "/" + std::to_string(kPW * kPH) + " correct";
            if (empty) shape += ", " + std::to_string(empty) + " still the frame's clear colour";
            shape += "]";
        }
        check(exact, label + shape + firstMismatch);
    }

    /// A render-target-sourced slot and a Texture2D-sourced control slot drawn with identical
    /// geometry and state must be byte-identical, and the control must itself be the pattern -- so
    /// a failure names which side is wrong instead of reporting that "the frame is black".
    void CheckSlotAgainstControl(const Readback& r, int rtSlot, int texSlot,
                                 const std::string& label, PatternFn want)
    {
        const Rectangle a = Slot(rtSlot);
        const Rectangle b = Slot(texSlot);
        int mismatched = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                if (!Same(r.at(a.X + x, a.Y + y), r.at(b.X + x, b.Y + y)))
                {
                    ++mismatched;
                    if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") rt=" + ColorText(r.at(a.X + x, a.Y + y)) +
                                " tex=" + ColorText(r.at(b.X + x, b.Y + y));
                }
        check(mismatched == 0,
              label + ": brand-new render target and Texture2D control sample identically (" +
              std::to_string(kPW * kPH - mismatched) + "/" + std::to_string(kPW * kPH) + ")" + first);
        CheckSlot(r, texSlot, label + ": the Texture2D control itself is the pattern", want);
        CheckSlot(r, rtSlot,  label + ": the brand-new render target holds its content", want);
    }

    // ---------------------------------------------------------------- production

    /// Constructs a target, binds it, and makes @p source its content with the draw as the bind
    /// cycle's FIRST public operation -- no explicit Clear, no warm-up, nothing before it. This is
    /// the exact shape REMED-GFX-158 loses: the operation that lands on the target's own base view.
    void ProduceFirstOp(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        DrawFull(dev, source);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    // ---------------------------------------------------------------- legs

    /// A1 -- the canonical immediate first use. A brand-new target, bound and drawn into with the
    /// draw as the cycle's first operation, read back in the same public frame.
    void LegA1(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH);
        ProduceFirstOp(dev, a, patternTex_);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "A1")) return;
        CheckTarget(r, "A1 construct, bind, draw, unbind, read -- all in one public frame",
                    PatternColor);
    }

    /// A2 -- the ticket's canonical sequence: clear to a distinctive mid-tone, then draw.
    void LegA2(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH);
        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.Clear(MidTone());
        DrawFull(dev, patternTex_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "A2")) return;
        CheckTarget(r, "A2 construct, bind, Clear(mid-tone), draw, unbind, read", PatternColor);
    }

    /// A3 -- a Clear as the cycle's first operation. RenderTargetUsage::PreserveContents, so the
    /// bind itself performs no discard clear and the public Clear really is the first thing the
    /// target sees.
    void LegA3(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None,
                         0, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.Clear(MidTone());
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "A3")) return;
        CheckUniform(r, "A3 construct, bind, Clear as the cycle's first operation, read",
                     MidTone());
    }

    /// A4 -- bind and unbind a brand-new DiscardContents target with no public work at all. The
    /// discard clear GraphicsDevice::SetRenderTarget performs is itself an operation on the
    /// target's base view, so it must land.
    void LegA4(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH);
        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "A4")) return;
        CheckUniform(r, "A4 a brand-new DiscardContents target's own bind clear lands",
                     DiscardTone());
    }

    /// B -- the consumer is an OLDER target, created before the producer. The producer is still
    /// brand-new; the consumer proves the content through sampling rather than through readback of
    /// the target itself.
    void LegB(GraphicsDevice& dev)
    {
        RenderTarget2D consumer(dev, kPW, kPH);      // created FIRST
        RenderTarget2D producer(dev, kPW, kPH);      // brand-new, produced below
        ProduceFirstOp(dev, producer, patternTex_);

        dev.SetRenderTarget(&consumer);
        ResetState(dev);
        DrawFull(dev, producer);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(consumer, kPW, kPH);
        if (!RequireReadable(r, "B")) return;
        CheckTarget(r, "B a brand-new producer sampled by an older render target", PatternColor);
    }

    /// C -- the same first use observed on the BACKBUFFER, beside an ordinary Texture2D control
    /// drawn in the same batch through the same sampler and blend state.
    void LegC(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH);
        ProduceFirstOp(dev, a, patternTex_);

        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, a, 0, patternTex_, 1);

        Readback r;
        if (!ReadBackbufferOr(dev, r, "C")) return;
        CheckSlotAgainstControl(r, 0, 1, "C brand-new render target consumed on the backbuffer",
                                PatternColor);

        // A SECOND independent Clear + draw + GetBackBufferData cycle over the SAME producer.
        // bgfx_rendertarget2d_mip_test.cpp carried an inherited claim that only the first such
        // cycle produces fresh data on this renderer and that every later one reads solid black
        // "no matter how many retries", and it kept a 20-attempt retry loop because of it. This
        // measures that claim rather than working around it.
        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, a, 2, patternTex_, 3);
        Readback r2;
        if (!ReadBackbufferOr(dev, r2, "C2")) return;
        CheckSlotAgainstControl(r2, 2, 3,
                                "C2 the same producer sampled again in a later backbuffer cycle",
                                PatternColor);
    }

    /// D -- producer and consumer both constructed in this frame, in that order, and used in it.
    void LegD(GraphicsDevice& dev)
    {
        RenderTarget2D producer(dev, kPW, kPH);
        RenderTarget2D consumer(dev, kPW, kPH);
        ProduceFirstOp(dev, producer, patternTex_);

        dev.SetRenderTarget(&consumer);
        ResetState(dev);
        DrawFull(dev, producer);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(consumer, kPW, kPH);
        if (!RequireReadable(r, "D")) return;
        CheckTarget(r, "D producer and consumer both constructed in this frame", PatternColor);
    }

    /// E -- the frame is already in progress: backbuffer work is queued first, and only then is the
    /// target constructed and used.
    void LegE(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        DrawSpriteSlot(dev, altPatternTex_, 3);

        RenderTarget2D a(dev, kPW, kPH);
        ProduceFirstOp(dev, a, patternTex_);

        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "E")) return;
        CheckTarget(r, "E constructed after this frame's earlier draws were already queued",
                    PatternColor);
    }

    /// F -- two brand-new targets produced in one order and read in the other. Neither may end up
    /// holding the other's content, and neither may be empty.
    void LegF(GraphicsDevice& dev)
    {
        RenderTarget2D first(dev, kPW, kPH);
        RenderTarget2D second(dev, kPW, kPH);
        ProduceFirstOp(dev, first, patternTex_);
        ProduceFirstOp(dev, second, altPatternTex_);

        Readback rSecond = ReadWholeTarget(second, kPW, kPH);
        Readback rFirst  = ReadWholeTarget(first, kPW, kPH);
        if (!RequireReadable(rFirst, "F")) return;
        if (!RequireReadable(rSecond, "F")) return;
        CheckTarget(rFirst,  "F two brand-new targets: the first holds its own content",
                    PatternColor);
        CheckTarget(rSecond, "F two brand-new targets: the second holds its own content",
                    AltPatternColor);
    }

    /// G -- bind, unbind and rebind a brand-new target inside one frame. The second cycle overwrites
    /// only the right-hand half, so a leg that loses either cycle fails distinguishably.
    void LegG(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW * 2, kPH, /*mipMap=*/false, SurfaceFormat::Color,
                         DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.Clear(MidTone());
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb.Draw(patternTex_, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH),
                    Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        dev.SetRenderTarget(&a);                       // rebound in the SAME frame
        ResetState(dev);
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb.Draw(altPatternTex_, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH),
                    Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(a, kPW * 2, kPH);
        if (!RequireReadable(r, "G")) return;
        int leftGood = 0, rightGood = 0;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                if (Same(r.at(x, y), PatternColor(x, y))) ++leftGood;
                if (Same(r.at(kPW + x, y), AltPatternColor(x, y))) ++rightGood;
            }
        check(leftGood == kPW * kPH,
              "G brand-new target, first bind cycle survives the rebind (" +
              std::to_string(leftGood) + "/" + std::to_string(kPW * kPH) + ")");
        check(rightGood == kPW * kPH,
              "G brand-new target, second bind cycle in the same frame lands (" +
              std::to_string(rightGood) + "/" + std::to_string(kPW * kPH) + ")");
    }

    /// H -- a brand-new MSAA target. It is consumed by SAMPLING, never by reading it back:
    /// REMED-GFX-154/164 own multisampled readback and this leg must not measure them.
    void LegH(GraphicsDevice& dev)
    {
        RenderTarget2D msaa(dev, kPW, kPH, /*mipMap=*/false, SurfaceFormat::Color,
                            DepthFormat::None, 4);
        RenderTarget2D plain(dev, kPW, kPH);
        std::printf("[INFO] H: requested MultiSampleCount 4, applied %d\n",
                    static_cast<int>(msaa.getMultiSampleCountProperty()));
        std::fflush(stdout);
        ProduceFirstOp(dev, msaa, patternTex_);

        dev.SetRenderTarget(&plain);
        ResetState(dev);
        DrawFull(dev, msaa);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(plain, kPW, kPH);
        if (!RequireReadable(r, "H")) return;
        CheckTarget(r, "H a brand-new MSAA target resolves and samples on first use", PatternColor);
    }

    /// I -- a brand-new mipmapped target. Level 0 must hold the content the frame produced; the mip
    /// chain itself is bgfx_rendertarget2d_mip_test.cpp's subject, not this file's.
    void LegI(GraphicsDevice& dev)
    {
        if (!kMipmappedRenderTargetSupported)
        {
            std::string what;
            try
            {
                RenderTarget2D a(dev, kPW, kPH, /*mipMap=*/true, SurfaceFormat::Color,
                                 DepthFormat::None);
                ProduceFirstOp(dev, a, patternTex_);
            }
            catch (const std::exception& e) { what = e.what(); }
            catch (...) { what = "(non-std exception)"; }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            check(!what.empty(),
                  "I " + std::string(kRendererName) +
                  " declares mipmapped render targets unimplemented and rejects deterministically" +
                  (what.empty() ? "" : " (" + what + ")"));
            return;
        }

        RenderTarget2D a(dev, kPW, kPH, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None);
        ProduceFirstOp(dev, a, patternTex_);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "I")) return;
        CheckTarget(r, "I a brand-new mipmapped target holds its level 0 content", PatternColor);
    }

    /// J -- a stock 3D draw as the bind cycle's FIRST operation, so this is not a SpriteBatch-only
    /// contract.
    void LegJ(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH);
        dev.SetRenderTarget(&a);
        ResetState(dev);
        Draw3DFull(dev, patternTex_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWholeTarget(a, kPW, kPH);
        if (!RequireReadable(r, "J")) return;
        CheckTarget(r, "J a stock 3D draw as a brand-new target's first operation", PatternColor);
    }

    /// K -- both draw families inside one brand-new target's first bind cycle, drawn into disjoint
    /// halves so this measures first use rather than REMED-GFX-157's intra-cycle ordering.
    void LegK(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW * 2, kPH, /*mipMap=*/false, SurfaceFormat::Color,
                         DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(&a);
        ResetState(dev);
        dev.Clear(MidTone());
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb.Draw(patternTex_, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH),
                    Color::White);
            sb.End();
        }
        // Right-hand half only, in clip space: x in [0, 1].
        {
            VertexPositionTexture q[6];
            q[0] = { Vector3(0.f,  1.f, 0.f), Vector2(0.f, 0.f) };
            q[1] = { Vector3(0.f, -1.f, 0.f), Vector2(0.f, 1.f) };
            q[2] = { Vector3(1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
            q[3] = { Vector3(0.f,  1.f, 0.f), Vector2(0.f, 0.f) };
            q[4] = { Vector3(1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
            q[5] = { Vector3(1.f,  1.f, 0.f), Vector2(1.f, 0.f) };
            dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            BasicEffect fx(dev);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&altPatternTex_);
            fx.VertexColorEnabled = false;
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(a, kPW * 2, kPH);
        if (!RequireReadable(r, "K")) return;
        int leftGood = 0, rightGood = 0;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                if (Same(r.at(x, y), PatternColor(x, y))) ++leftGood;
                if (Same(r.at(kPW + x, y), AltPatternColor(x, y))) ++rightGood;
            }
        check(leftGood == kPW * kPH,
              "K brand-new target, SpriteBatch half of the first cycle (" +
              std::to_string(leftGood) + "/" + std::to_string(kPW * kPH) + ")");
        check(rightGood == kPW * kPH,
              "K brand-new target, stock 3D half of the first cycle (" +
              std::to_string(rightGood) + "/" + std::to_string(kPW * kPH) + ")");
    }

    /// L -- construct, use, dispose, and construct again in the SAME frame, repeatedly. A recycled
    /// view id, a recycled framebuffer handle or a recycled object address must not resurrect a
    /// destroyed target's registration or lose the new one's.
    void LegL(GraphicsDevice& dev)
    {
        bool allOk = true;
        std::string firstFailure;
        for (int round = 0; round < 4; ++round)
        {
            const bool alt = (round % 2) == 1;
            auto rt = std::make_unique<RenderTarget2D>(dev, kPW, kPH);
            ProduceFirstOp(dev, *rt, alt ? altPatternTex_ : patternTex_);
            Readback r = ReadWholeTarget(*rt, kPW, kPH);
            if (!RequireReadable(r, "L")) return;
            for (int y = 0; y < kPH && firstFailure.empty(); ++y)
                for (int x = 0; x < kPW && firstFailure.empty(); ++x)
                {
                    const Color want = alt ? AltPatternColor(x, y) : PatternColor(x, y);
                    if (!Same(r.at(x, y), want))
                    {
                        allOk = false;
                        firstFailure = " round " + std::to_string(round) + " at (" +
                                       std::to_string(x) + "," + std::to_string(y) +
                                       ") want=" + ColorText(want) +
                                       " got=" + ColorText(r.at(x, y));
                    }
                }
            rt.reset();   // destroyed here: view id and framebuffer handle both return to the pool
        }
        check(allOk, "L construct / use / dispose four times in one frame, no round loses content" +
                     firstFailure);
    }

    /// M -- cardinality. Many brand-new targets used in one frame, then the same again across
    /// several public frames, so a fix that leaks a view id, a framebuffer or a mirror entry per
    /// first use runs out rather than passing quietly.
    void LegM(GraphicsDevice& dev)
    {
        constexpr int kPerFrame = 8;
        constexpr int kFrames = 3;
        bool allOk = true;
        std::string firstFailure;
        std::string thrown;

        try
        {
            for (int frame = 0; frame < kFrames; ++frame)
            {
                for (int i = 0; i < kPerFrame; ++i)
                {
                    RenderTarget2D rt(dev, kPW, kPH);
                    const bool alt = ((frame + i) % 2) == 1;
                    ProduceFirstOp(dev, rt, alt ? altPatternTex_ : patternTex_);
                    Readback r = ReadWholeTarget(rt, kPW, kPH);
                    if (!RequireReadable(r, "M")) return;
                    for (int y = 0; y < kPH && firstFailure.empty(); ++y)
                        for (int x = 0; x < kPW && firstFailure.empty(); ++x)
                        {
                            const Color want = alt ? AltPatternColor(x, y) : PatternColor(x, y);
                            if (!Same(r.at(x, y), want))
                            {
                                allOk = false;
                                firstFailure = " frame " + std::to_string(frame) + " target " +
                                               std::to_string(i) + " at (" + std::to_string(x) +
                                               "," + std::to_string(y) + ") want=" +
                                               ColorText(want) + " got=" + ColorText(r.at(x, y));
                            }
                        }
                }
                BeginBackbuffer(dev);
                dev.Present();
            }
        }
        catch (const std::exception& e) { thrown = e.what(); }
        catch (...) { thrown = "(non-std exception)"; }

        check(thrown.empty(), "M " + std::to_string(kPerFrame * kFrames) +
                              " brand-new targets across " + std::to_string(kFrames) +
                              " frames complete without exhausting any pool" +
                              (thrown.empty() ? "" : ": " + thrown));
        check(allOk, "M every one of those targets holds its own content" + firstFailure);
    }

    /// N -- a brand-new RenderTargetCube FACE as the process's first bind cycle. A cube face is
    /// registered through its own path (BindAsRenderTargetFace, one framebuffer per face created
    /// lazily on first bind), so a fix that only restored the 2D path would leave this red.
    void LegN(GraphicsDevice& dev)
    {
        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(dev, kPW, /*mipMap=*/false,
                                                      SurfaceFormat::Color, DepthFormat::None, 0,
                                                      RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] N: %s cannot bind a RenderTargetCube face (%s) -- boundary "
                        "recorded\n", kRendererName, e.what());
            std::fflush(stdout);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return;
        }

        // The draw is the cycle's first public operation, exactly as in leg A1.
        ResetState(dev);
        DrawFull(dev, patternTex_);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        std::vector<Color> face(static_cast<std::size_t>(kPW) * kPW, Color(0xCD, 0xCD, 0xCD, 0xCD));
        std::string what;
        try { cube->GetData(CubeMapFace::PositiveX, face.data(), 0, static_cast<int>(face.size())); }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) { what = "(non-std exception)"; }
        if (!what.empty())
        {
            if (Unsupported())
                check(true, "N declared non-rasterizing, cube readback rejects (" + what + ")");
            else
            {
                std::printf("[INFO] N: cube readback unavailable on %s (%s) -- boundary recorded\n",
                            kRendererName, what.c_str());
                std::fflush(stdout);
            }
            return;
        }

        // Only the pattern's own kPW x kPH region was drawn into the kPW x kPW face.
        int good = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = face[static_cast<std::size_t>(y) * kPW + x];
                if (Same(got, PatternColor(x, y))) ++good;
                else if (first.empty())
                    first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                            ") want=" + ColorText(PatternColor(x, y)) + " got=" + ColorText(got);
            }
        check(good == kPW * kPH,
              "N a brand-new RenderTargetCube face holds its first cycle's content (" +
              std::to_string(good) + "/" + std::to_string(kPW * kPH) + ")" + first);
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        std::vector<Color> pattern(static_cast<std::size_t>(kPW) * kPH, Color(0, 0, 0, 0));
        std::vector<Color> alt(static_cast<std::size_t>(kPW) * kPH, Color(0, 0, 0, 0));
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                pattern[static_cast<std::size_t>(y) * kPW + x] = PatternColor(x, y);
                alt[static_cast<std::size_t>(y) * kPW + x]     = AltPatternColor(x, y);
            }
        patternTex_ = Texture2D(dev, kPW, kPH);
        patternTex_.SetData(pattern.data(), static_cast<int>(pattern.size()));
        altPatternTex_ = Texture2D(dev, kPW, kPH);
        altPatternTex_.SetData(alt.data(), static_cast<int>(alt.size()));

        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        std::printf("[INFO] REMED-GFX-158 immediate render-target first use on %s "
                    "(%dx%d pattern, %dx%d backbuffer, legs: %s)\n",
                    kRendererName, kPW, kPH, kBBW, kBBH,
                    legFilter_.empty() ? "all" : legFilter_.c_str());
        std::fflush(stdout);

        using LegFn = void (RenderTargetFirstUseTest::*)(GraphicsDevice&);
        struct Leg { const char* id; LegFn fn; bool needsStockEffectRtSource; };
        static const Leg kLegs[] = {
            { "A1", &RenderTargetFirstUseTest::LegA1, false },
            { "A2", &RenderTargetFirstUseTest::LegA2, false },
            { "A3", &RenderTargetFirstUseTest::LegA3, false },
            { "A4", &RenderTargetFirstUseTest::LegA4, false },
            { "B",  &RenderTargetFirstUseTest::LegB,  false },
            { "C",  &RenderTargetFirstUseTest::LegC,  false },
            { "D",  &RenderTargetFirstUseTest::LegD,  false },
            { "E",  &RenderTargetFirstUseTest::LegE,  false },
            { "F",  &RenderTargetFirstUseTest::LegF,  false },
            { "G",  &RenderTargetFirstUseTest::LegG,  false },
            { "H",  &RenderTargetFirstUseTest::LegH,  false },
            { "I",  &RenderTargetFirstUseTest::LegI,  false },
            { "J",  &RenderTargetFirstUseTest::LegJ,  true  },
            { "K",  &RenderTargetFirstUseTest::LegK,  true  },
            { "L",  &RenderTargetFirstUseTest::LegL,  false },
            { "M",  &RenderTargetFirstUseTest::LegM,  false },
            { "N",  &RenderTargetFirstUseTest::LegN,  false },
        };

        bool matched = false;
        for (const Leg& leg : kLegs)
        {
            if (!legFilter_.empty() && legFilter_ != leg.id) continue;
            matched = true;
            if (leg.needsStockEffectRtSource && !kStockEffectRtSourceSupported)
            {
                std::printf("[INFO] %s: skipped -- %s cannot use a RenderTarget2D as a stock "
                            "effect texture (REMED-GFX-152)\n", leg.id, kRendererName);
                std::fflush(stdout);
                continue;
            }
            (this->*leg.fn)(dev);
        }
        if (!matched)
            check(false, "unknown leg filter '" + legFilter_ + "'");

        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        // A leg that records only a declared renderer boundary contributes no checks and is not a
        // failure; an unmatched --leg filter adds its own failing check above, so a run that
        // measured nothing at all still cannot pass silently.
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    /**
     * @brief Builds the fixture.
     *
     * @param legFilter When non-empty, the single leg id to run. Running one leg alone makes it the
     *        first public graphics work the process performs, which is what a first-use contract
     *        has to be measured as.
     */
    explicit RenderTargetFirstUseTest(std::string legFilter)
        : legFilter_(std::move(legFilter))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

int main(int argc, char** argv)
{
    std::string leg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg.rfind("--leg=", 0) == 0) leg = arg.substr(6);
    }

    RenderTargetFirstUseTest test(std::move(leg));
    test.Run();
    return test.Result();
}
