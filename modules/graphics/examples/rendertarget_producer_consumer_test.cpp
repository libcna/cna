// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-151: the canonical XNA render-to-texture sequence must work with NO intervening
// synchronous operation.
//
//     SetRenderTarget(A)                 // producer
//     draw a pattern into A
//     SetRenderTarget(B or nullptr)
//     draw, sampling A as a texture      // consumer
//     ... observe the consumer's output
//
// The public contract, in CNA/XNA/FNA terms: **a render target becomes sampleable the moment it is
// unbound.** The game must not need GetData, Present, a second frame, a manual flush, a fence wait,
// a device/queue idle or a CPU readback to make the producer's result visible to the consumer.
//
// THE DEFECT this file reproduces is Vulkan-local. The Vulkan renderer is a whole-frame-deferred
// recorder: every Clear/sprite/3D call is queued and replayed once, at Present, into one command
// buffer whose passes are ordered by the public bind-cycle ("segment") id. A GetData readback of a
// target cannot wait for Present, so REMED-GFX-074 added an early flush that records and submits
// that target's passes immediately -- and, to avoid double-rendering at Present, it consumes what
// it recorded. That flush filters the frame's segment list down to the target being READ:
//
//     if (rtOnly) erase every segment whose rt is not in this target's flush group;
//
// A producer target A is a different target from the destination B being read, so A's producer
// segment was filtered out. B's consumer pass therefore ran with A's colour image never having been
// rendered into at all, and A's producer pass was only recorded later, at Present -- after the read
// that observed it. Inserting `A.GetData(...)` between producer and consumer repaired the sequence
// because THAT flush records A's own segment, so by the time B's flush ran, A's image genuinely held
// the pattern. GetData was acting as an accidental execution barrier; the missing native operation
// was the producer's render pass itself, not a wait.
//
// So the fix is NOT a flush/wait/readback: the early flush replays every pending render-target
// segment up to the flush point, in public segment order, exactly as Present would. The image
// layout and memory dependency were already correct (each render target's render pass ends with
// finalLayout = SHADER_READ_ONLY_OPTIMAL and a subpass dependency
// COLOR_ATTACHMENT_OUTPUT/COLOR_ATTACHMENT_WRITE -> FRAGMENT_SHADER|TRANSFER /
// SHADER_READ|TRANSFER_READ), which is why nothing here needed a new barrier.
//
// THE ORACLE. Every leg compares the consumer's output against an ordinary Texture2D control that
// holds the producer's expected bytes and is drawn through the identical geometry, sampler and
// blend state in the same frame. The control is built from the pattern function directly, NEVER
// from the producer's own GetData -- reading the producer is precisely the operation under test, and
// a fixture that read it would materialise the very state it is supposed to prove unnecessary.
//
// THE PATTERN is 8x4 -- deliberately NON-SQUARE, so a transpose cannot masquerade as a pass -- and
// every one of its 32 texels is unique in (R, G). A stale buffer, a zero buffer, a uniform fill, a
// mirror, a rotation and a swapped resource each fail distinguishably.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A  canonical producer -> consumer      the finding itself; no GetData of the source anywhere
//   B  accidental-barrier diagnostic       the same sequence WITH a source GetData, printed for
//                                          comparison; post-fix both must agree
//   C  stock 3D consumers                  BasicEffect (indexed + non-indexed), AlphaTestEffect,
//                                          DualTextureEffect -- resource/state fix, not SpriteBatch
//   D  target chains                       texture->A->B, A->B->C, A twice, A->backbuffer,
//                                          A->B->A, two producers consumed in reverse order
//   E  MSAA producer                       the resolve must complete without a readback
//   F  mipmapped producer                  mip generation must happen at unbind, not at readback
//   G  Preserve/Discard/Platform + Clear    clear-only, geometry-only, clear-then-draw,
//                                          draw-then-clear producers
//   H  SpriteBatch knobs                   source rectangle, PointClamp and LinearClamp
//   I  backbuffer boundary                 a mid-frame readback must not advance what a pending
//                                          backbuffer draw sees of a target it sampled earlier
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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#if defined(CNA_RENDERER_VULKAN)
// REMED-GFX-151 is a Vulkan finding, so the Vulkan build of this fixture additionally judges the
// Khronos validation layer -- standard AND synchronization checks -- over the whole matrix above.
// The fix reorders when render passes are recorded, which is exactly the kind of change that can
// introduce a read-after-write hazard without changing a single pixel on a permissive driver.
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"
#endif

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief Whether this renderer rasterizes and can read a render target back at all.
     *
     * HEADLESS performs no rasterization, so REMED-GFX-127's contract makes its `GetData` reject
     * deterministically. There is no producer result to observe there; the legs below assert the
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
#elif defined(CNA_RENDERER_SOKOL)
    // plans/plan_sokol.md SOKOL-25/38: real geometry really is rasterized into a RenderTarget2D here, and
    // `SokolRenderTargetRenderer::GetData` now round-trips real content via a throwaway GL FBO
    // (docs/sokol-renderer.md), so `kRasterizes = true` is accurate for this file's contract too.
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "SOKOL";
#elif defined(CNA_RENDERER_LLGL)
    constexpr bool kRasterizes = true;
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-151: this renderer has no declared render-target producer/consumer contract."
#endif

    /**
     * @brief Whether a RenderTarget2D may be handed to a stock 3D effect as its texture.
     *
     * REMED-GFX-152 CLOSED this (2026-07-29), and the declaration is now unconditionally true.
     *
     * It used to read false on SDL_GPU, whose stock-effect paths
     * `static_cast<const SdlGpuTextureRenderer*>(params.textureN)` onto the unrelated sibling
     * SdlGpuRenderTargetRenderer and died. Leg C (four stock 3D consumers) was skipped there and now
     * runs: SIGSEGV against the pre-fix renderer, 4/4 exact after.
     */
    constexpr bool kStockEffectRtSourceSupported = true;

    // kStockEffectRtSourceSupported never gates GetData: it is read only where kRasterizes is
    // already true, which is never SOKOL's case above, so its value is moot for this renderer and
    // left at the shared default rather than given a branch of its own.

    /**
     * @brief Whether a BACKBUFFER draw can sample a render target that was never read back.
     *
     * REMED-GFX-155, measured here and FIXED (2026-07-29), so this is now true everywhere. On bgfx
     * a render target produced and unbound in this frame sampled as entirely empty when the
     * consumer drew to the BACKBUFFER (0/32, all 32 texels (0,0,0,0)), while the identical source
     * sampled into another RENDER TARGET was byte-exact (legs D1-D5) and an ordinary Texture2D
     * drawn in the same backbuffer batch was byte-exact too. The cause was neither an empty target
     * nor a broken backbuffer draw: bgfx radix-sorts a frame's draws by their view's sort position,
     * which defaults to the numeric view id, and that renderer's id partition puts the backbuffer at
     * 0 below every render target -- so the consumer executed BEFORE its producer. The renderer now
     * records the frame's views in public first-use order and programs it through
     * bgfx::setViewOrder. `rendertarget_backbuffer_consumer_test.cpp` is that contract's own
     * fixture; this declaration is what pins it from here.
     */
    constexpr bool kBackbufferConsumerSeesNeverReadTarget = true;

    /**
     * @brief Whether a `Clear()` issued AFTER a draw, in the same bind cycle, wins.
     *
     * REMED-GFX-156 measured this here first: on SDL_GPU **and WebGPU** a producer built as
     * `Clear(A); draw pattern; Clear(B)` used to be sampled by its consumer as the PATTERN, not as
     * B -- the trailing clear was lost, so `Clear()` was not an ordered command there. Both
     * reported the identical value (20,25,40,255), i.e. the pattern's own texel (0,0), on all three
     * RenderTargetUsage values, which is the SDL_GPU/WebGPU counterpart of REMED-GFX-129 (fixed on
     * Vulkan and scoped to Vulkan). Both renderers now cut their bind cycle into one native render
     * pass per observable Clear, so the ordered contract holds everywhere and this declaration is
     * unconditional -- the producer/consumer path being the second, independent witness of it.
     */
    constexpr bool kOrderedClearAfterDrawWins = true;

    /**
     * @brief Whether `DualTextureEffect` accepts this fixture's `VertexPositionTexture` geometry.
     *
     * D3D9 rejects it outright -- "stride 20 with vertexColor=false has no matching CNA vertex
     * layout (plans/plan_dx9.md D9-82d)" -- which is a documented, pre-existing vertex-layout boundary of
     * that renderer and nothing to do with render-to-texture. The other three stock-effect cases
     * (BasicEffect indexed and non-indexed, AlphaTestEffect) still run there and are what carry the
     * "not SpriteBatch-only" claim on D3D9.
     */
    constexpr bool kDualTextureAcceptsPositionTexture =
#if defined(CNA_RENDERER_DIRECTX9)
        false;
#elif defined(CNA_RENDERER_SOKOL)
        // Not a vertex-layout rejection: SokolRenderer::DrawColored3D refuses ANY
        // `params.dualTexture` draw outright (plans/plan_sokol.md -- dual-texture 3D is not implemented
        // yet, unlike the single-texture Textured/Lit paths SOKOL-21 landed), so C4 would throw
        // uncaught here rather than reject cleanly. Reusing this flag to skip it is the same
        // "documented boundary, unrelated to render-to-texture" carve-out D3D9 already uses.
        false;
#else
        true;
#endif

    constexpr int kBBW = 64;   ///< Backbuffer width.
    constexpr int kBBH = 64;   ///< Backbuffer height.

    constexpr int kPW = 8;     ///< Pattern width.  Deliberately different from kPH.
    constexpr int kPH = 4;     ///< Pattern height. A transpose therefore cannot pass.

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
        const int g = 235 - y * 70;                       // 235, 165, 95, 25
        const int b = 30 + ((x * 3 + y * 5) % 7) * 30;
        const int a = 90 + ((x + 3 * y) % 4) * 55;       // 90, 145, 200, 255
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

    /** @brief Exact byte equality on all four channels. */
    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

/**
 * @brief REMED-GFX-151's public same-frame render-to-texture producer/consumer contract.
 */
class RenderTargetProducerConsumerTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;

    Texture2D whiteTex_;      ///< 1x1 opaque white, for solid fills and the DualTexture identity.
    Texture2D patternTex_;    ///< kPW x kPH, SetData of PatternColor -- what a producer must contain.
    Texture2D altPatternTex_; ///< kPW x kPH, SetData of AltPatternColor.

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

    // ---------------------------------------------------------------- readback

    /** @brief The outcome of a public GetData call. */
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

    /// Reads a whole target back. The destination is pre-filled with a poison value so a renderer
    /// that writes nothing cannot be mistaken for one that wrote transparent black.
    static Readback ReadWhole(RenderTarget2D& target, int w, int h)
    {
        Readback r;
        r.w = w;
        r.h = h;
        r.pixels.assign(static_cast<std::size_t>(w) * h, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(r.pixels.data(), 0, static_cast<int>(r.pixels.size()));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /// True when this renderer declared it cannot rasterize/read targets at all.
    static bool Unsupported() { return !kRasterizes; }

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

    // ---------------------------------------------------------------- drawing

    /// Draws @p source 1:1 into the currently bound target with point sampling.
    void BlitPattern(GraphicsDevice& dev, const Texture2D& source)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(source, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws @p source 1:1 at destination x offset @p dstX.
    void BlitPatternAt(GraphicsDevice& dev, const Texture2D& source, int dstX,
                       SamplerState sampler = SamplerState::PointClamp,
                       const std::optional<Rectangle>& src = std::nullopt)
    {
        SpriteBatch sb(dev);
        SamplerState s = sampler;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &s, nullptr, nullptr);
        const Rectangle useSrc = src.value_or(Rectangle(0, 0, kPW, kPH));
        sb.Draw(source, Rectangle(dstX, 0, useSrc.Width, useSrc.Height), useSrc, Color::White);
        sb.End();
    }

    /// Produces the pattern inside @p rt through ordinary (never-defective) Texture2D sampling and
    /// unbinds it. The target is NEVER read back here -- that is the whole point of this fixture.
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, source);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    /// The standard XNA texture-on-quad UV map: NDC top-left -> UV (0, 0).
    static void FillSamplingQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    /// Indexed form of the same quad -- four vertices, six indices.
    static void FillSamplingQuadIndexed(VertexPositionTexture* q, std::uint16_t* idx)
    {
        q[0] = { Vector3(-1.f,  1.f, 0.f), Vector2(0.f, 0.f) };
        q[1] = { Vector3(-1.f, -1.f, 0.f), Vector2(0.f, 1.f) };
        q[2] = { Vector3( 1.f, -1.f, 0.f), Vector2(1.f, 1.f) };
        q[3] = { Vector3( 1.f,  1.f, 0.f), Vector2(1.f, 0.f) };
        idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
    }

    // ---------------------------------------------------------------- comparisons

    using PatternFn = Color (*)(int, int);

    /// Asserts a kPW x kPH region of @p r starting at @p x0 reproduces @p want exactly, and names
    /// the failure shape (empty / stale / uniform) rather than only reporting a count.
    void CheckRegion(const Readback& r, int x0, const std::string& label, PatternFn want)
    {
        int good = 0, empty = 0, poison = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = r.at(x0 + x, y);
                if (Same(got, want(x, y))) ++good;
                else if (firstMismatch.empty())
                    firstMismatch = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") want=" + ColorText(want(x, y)) + " got=" + ColorText(got);
                if (got.getRProperty() == 0 && got.getGProperty() == 0 &&
                    got.getBProperty() == 0 && got.getAProperty() == 0) ++empty;
                if (got.getRProperty() == 0xCD && got.getGProperty() == 0xCD &&
                    got.getBProperty() == 0xCD && got.getAProperty() == 0xCD) ++poison;
            }
        const bool exact = good == kPW * kPH;
        std::string shape;
        if (!exact)
        {
            shape = " [" + std::to_string(good) + "/" + std::to_string(kPW * kPH) + " correct";
            if (empty)  shape += ", " + std::to_string(empty)  + " entirely empty";
            if (poison) shape += ", " + std::to_string(poison) + " never written";
            shape += "]";
        }
        check(exact, label + shape + firstMismatch);
    }

    /// The canonical oracle: a render-target-sourced region and a Texture2D-sourced region drawn
    /// with identical geometry and state must be byte-identical, and the control must itself be
    /// the pattern (so a failure names which side is wrong).
    void CheckAgainstControl(const Readback& r, int rtX, int texX, const std::string& label,
                             PatternFn want)
    {
        int mismatched = 0;
        std::string first;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
                if (!Same(r.at(rtX + x, y), r.at(texX + x, y)))
                {
                    ++mismatched;
                    if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") rt=" + ColorText(r.at(rtX + x, y)) +
                                " tex=" + ColorText(r.at(texX + x, y));
                }
        check(mismatched == 0,
              label + ": render-target source and Texture2D control sample identically (" +
              std::to_string(kPW * kPH - mismatched) + "/" + std::to_string(kPW * kPH) + ")" + first);
        CheckRegion(r, texX, label + ": the Texture2D control itself is the pattern", want);
        CheckRegion(r, rtX,  label + ": the render-target source is the producer's content", want);
    }

    /// Prints the first row of a region, so the measured content is on the record rather than
    /// inferred from a pass/fail line.
    static void PrintRow(const Readback& r, int x0, const char* label)
    {
        if (!r.ok()) return;
        std::printf("[INFO] %s row0:", label);
        for (int x = 0; x < kPW && x0 + x < r.w; ++x)
            std::printf(" %s", ColorText(r.at(x0 + x, 0)).c_str());
        std::printf("\n");
        std::fflush(stdout);
    }

    // ================================================================ legs

    /**
     * @brief Leg A -- the finding. Render into A, unbind, sample A, observe. No source readback.
     *
     * The destination is twice the pattern width: the left half is drawn from the render target,
     * the right half from an ordinary Texture2D holding the same expected bytes, in ONE batch with
     * identical rectangles and sampler. One readback of the DESTINATION decides both.
     */
    void LegCanonical(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(a,           Rectangle(0,   0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.Draw(patternTex_, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW * 2, kPH);
        if (!RequireReadable(r, "A1 canonical producer -> consumer")) return;
        PrintRow(r, 0,   "A1 render-target source");
        PrintRow(r, kPW, "A1 Texture2D control ");
        CheckAgainstControl(r, 0, kPW,
                            "A1 SetRenderTarget(A); draw; unbind; sample A with no GetData", PatternColor);
    }

    /**
     * @brief Leg B -- the accidental barrier, kept as a diagnostic rather than as the fix.
     *
     * Identical to leg A except one `A.GetData(...)` is issued between producer and consumer. Pre-fix
     * this was the ONLY difference that made the sequence work, which is what identified GetData as
     * an accidental execution barrier; post-fix the two legs must agree, and this leg asserts that
     * the extra readback neither repairs nor disturbs anything.
     */
    void LegBarrierDiagnostic(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        // The accidental barrier, issued deliberately and named as such.
        Readback source = ReadWhole(a, kPW, kPH);
        if (RequireReadable(source, "B1 diagnostic source GetData"))
            CheckRegion(source, 0, "B1 the producer's own readback is the pattern", PatternColor);

        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPattern(dev, a);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW, kPH);
        if (!RequireReadable(r, "B2 consumer after a source GetData")) return;
        CheckRegion(r, 0, "B2 an intervening source GetData changes nothing (leg A must already pass)",
                    PatternColor);
    }

    /**
     * @brief Leg C -- stock 3D consumers, so the fix cannot be SpriteBatch-local.
     *
     * Each case draws the producer across a whole destination through MESH UVs, then repeats the
     * identical draw with the ordinary-texture control. No shader change is expected or required:
     * the producer's content simply has to exist by the time the consumer's descriptor is used.
     */
    void Leg3DConsumers(GraphicsDevice& dev)
    {
        struct Case { const char* name; int kind; };
        const Case cases[] = {
            { "C1 BasicEffect textured (non-indexed)", 0 },
            { "C2 BasicEffect textured (indexed)",     1 },
            { "C3 AlphaTestEffect",                    2 },
            { "C4 DualTextureEffect slot 0",           3 },
        };

        if (!kStockEffectRtSourceSupported)
        {
            std::printf("[INFO] C legs: %s cannot hand a RenderTarget2D to a stock 3D effect "
                        "(REMED-GFX-152) -- boundary recorded, SpriteBatch legs still cover the "
                        "contract here\n", kRendererName);
            std::fflush(stdout);
            return;
        }

        for (const Case& c : cases)
        {
            if (c.kind == 3 && !kDualTextureAcceptsPositionTexture)
            {
                std::printf("[INFO] %s skipped on %s: DualTextureEffect rejects this fixture's "
                            "VertexPositionTexture stride (plans/plan_dx9.md D9-82d) -- a documented "
                            "vertex-layout boundary, unrelated to render-to-texture\n",
                            c.name, kRendererName);
                std::fflush(stdout);
                continue;
            }
            // A fresh producer per case: a case that only passed because an earlier case had
            // already materialised the same target would prove nothing.
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);

            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            Readback reads[2];
            for (int side = 0; side < 2; ++side)
            {
                Texture2D* src = (side == 0) ? static_cast<Texture2D*>(&a) : &patternTex_;
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
                dev.Clear(Color(0, 0, 0, 255));

                VertexPositionTexture q[6];
                std::uint16_t idx[6];
                FillSamplingQuad(q);

                if (c.kind == 2)
                {
                    AlphaTestEffect fx(dev);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureProperty(src);
                    fx.setAlphaFunctionProperty(CompareFunction::Greater);
                    fx.setReferenceAlphaProperty(0);
                    fx.Apply();
                    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                }
                else if (c.kind == 3)
                {
                    // Slot 1 carries an opaque-white 1x1, the multiplicative identity for this
                    // effect's base.rgb * 2 * second.rgb combine, so only slot 0 can move a texel.
                    DualTextureEffect fx(dev);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureProperty(src);
                    fx.setTexture2Property(&whiteTex_);
                    fx.Apply();
                    dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                }
                else
                {
                    BasicEffect fx(dev);
                    fx.setWorldProperty(Matrix::getIdentityProperty());
                    fx.setViewProperty(Matrix::getIdentityProperty());
                    fx.setProjectionProperty(Matrix::getIdentityProperty());
                    fx.setTextureEnabledProperty(true);
                    fx.setTextureProperty(src);
                    fx.VertexColorEnabled = false;
                    fx.Apply();
                    if (c.kind == 1)
                    {
                        FillSamplingQuadIndexed(q, idx);
                        dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, q, 0, 4, idx, 0, 2);
                    }
                    else
                    {
                        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
                    }
                }

                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                reads[side] = ReadWhole(dst, kPW, kPH);
            }

            if (!RequireReadable(reads[0], std::string(c.name) + " render-target source")) continue;
            if (!RequireReadable(reads[1], std::string(c.name) + " Texture2D control"))     continue;

            int mismatched = 0;
            std::string first;
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(reads[0].at(x, y), reads[1].at(x, y)))
                    {
                        ++mismatched;
                        if (first.empty())
                            first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                    ") rt=" + ColorText(reads[0].at(x, y)) +
                                    " tex=" + ColorText(reads[1].at(x, y));
                    }
            check(mismatched == 0,
                  std::string(c.name) + ": a never-read render target and its Texture2D control "
                  "sample identically (" + std::to_string(kPW * kPH - mismatched) + "/" +
                  std::to_string(kPW * kPH) + ")" + first);
        }
    }

    /**
     * @brief Leg D -- chains. Every hop is a producer for the next, all in one public frame.
     *
     * Distinct patterns are used wherever two live producers exist, so a stale or swapped resource
     * fails on every texel instead of on a few.
     */
    void LegChains(GraphicsDevice& dev)
    {
        // D1: ordinary texture -> A -> B. Only B is ever read.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, b, a);
            Readback r = ReadWhole(b, kPW, kPH);
            if (RequireReadable(r, "D1 texture -> A -> B"))
                CheckRegion(r, 0, "D1 texture -> RT A -> RT B, only B read", PatternColor);
        }

        // D2: A -> B -> C, three producers deep, only C read.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, b, a);
            RenderTarget2D c(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, c, b);
            Readback r = ReadWhole(c, kPW, kPH);
            if (RequireReadable(r, "D2 A -> B -> C"))
                CheckRegion(r, 0, "D2 RT A -> RT B -> RT C, only C read", PatternColor);
        }

        // D3: one producer consumed by two independent consumers.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, b, a);
            RenderTarget2D c(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, c, a);
            Readback rb = ReadWhole(b, kPW, kPH);
            Readback rc = ReadWhole(c, kPW, kPH);
            if (RequireReadable(rb, "D3 first consumer"))
                CheckRegion(rb, 0, "D3 producer A consumed by consumer B", PatternColor);
            if (RequireReadable(rc, "D3 second consumer"))
                CheckRegion(rc, 0, "D3 the same producer A consumed again by consumer C", PatternColor);
        }

        // D4: two independent producers with identical format/dimensions, consumed in the REVERSE
        // order they were produced. Two live targets that alias, or a consumer that picked up the
        // most recently produced target rather than the one it was handed, fails here.
        {
            RenderTarget2D p1(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            RenderTarget2D p2(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            ProduceInto(dev, p1, patternTex_);
            ProduceInto(dev, p2, altPatternTex_);

            RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                // p2 first, p1 second -- the reverse of the production order.
                sb.Draw(p2, Rectangle(0,   0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.Draw(p1, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW * 2, kPH);
            if (RequireReadable(r, "D4 two producers, reverse consumption order"))
            {
                CheckRegion(r, 0,   "D4 the second producer is sampled first and is its own content",
                            AltPatternColor);
                CheckRegion(r, kPW, "D4 the first producer is sampled second and is its own content",
                            PatternColor);
            }
        }

        // D5: A -> B -> A. The same target is a producer, then a consumer's destination, then read.
        // A target rebound after being sampled must open a genuinely new pass (REMED-GFX-140) whose
        // content is what the LAST cycle produced.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);       // A := pattern
            ProduceInto(dev, b, a);                 // B := A
            ProduceInto(dev, a, altPatternTex_);    // A := alt pattern (a new bind cycle)
            Readback rb = ReadWhole(b, kPW, kPH);
            Readback ra = ReadWhole(a, kPW, kPH);
            if (RequireReadable(rb, "D5 B captured A's first cycle"))
                CheckRegion(rb, 0, "D5 A -> B captured A's FIRST cycle, not its later one", PatternColor);
            if (RequireReadable(ra, "D5 A's second cycle"))
                CheckRegion(ra, 0, "D5 A rebound after being sampled holds its SECOND cycle",
                            AltPatternColor);
        }

        // D6: producer -> backbuffer consumer. GetBackBufferData is the honest oracle where it
        // exists; where it does not, the boundary is recorded rather than asserted either way.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
                sb.Draw(a,           Rectangle(0,   0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.Draw(patternTex_, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.End();
            }

            std::vector<Color> frame(static_cast<std::size_t>(kBBW) * kBBH,
                                     Color(0xCD, 0xCD, 0xCD, 0xCD));
            bool threw = false;
            std::string what;
            try { dev.GetBackBufferData(frame.data(), 0, static_cast<int>(frame.size())); }
            catch (const System::NotSupportedException&) { threw = true; what = "NotSupportedException"; }
            catch (const std::exception& e) { threw = true; what = e.what(); }

            if (threw || Unsupported())
            {
                std::printf("[INFO] D6 backbuffer consumer oracle unavailable on %s (%s) -- "
                            "boundary recorded; D1-D5 carry the contract\n",
                            kRendererName, threw ? what.c_str() : "non-rasterizing");
                std::fflush(stdout);
            }
            else
            {
                Readback r;
                r.w = kBBW; r.h = kBBH; r.pixels = frame;
                // The Texture2D control is asserted either way: it is what proves the backbuffer
                // draw itself works, so a declared boundary names the render-target source alone
                // rather than excusing the whole leg.
                CheckRegion(r, kPW, "D6 the Texture2D control on the backbuffer is the pattern",
                            PatternColor);
                if (kBackbufferConsumerSeesNeverReadTarget)
                {
                    CheckAgainstControl(r, 0, kPW, "D6 producer -> backbuffer consumer", PatternColor);
                }
                else
                {
                    int good = 0, empty = 0;
                    for (int y = 0; y < kPH; ++y)
                        for (int x = 0; x < kPW; ++x)
                        {
                            const Color& c = r.at(x, y);
                            if (Same(c, PatternColor(x, y))) ++good;
                            if (c.getRProperty() == 0 && c.getGProperty() == 0 &&
                                c.getBProperty() == 0 && c.getAProperty() == 0) ++empty;
                        }
                    check(good < kPW * kPH,
                          std::string("D6 REMED-GFX-155 pinned: a never-read render target reaching "
                          "a BACKBUFFER consumer does not reproduce the source on ") + kRendererName +
                          " (" + std::to_string(good) + "/" + std::to_string(kPW * kPH) +
                          " correct, " + std::to_string(empty) + " entirely empty) while the same "
                          "source into a render target is byte-exact above. Fixing it must flip "
                          "this declaration.");
                }
            }
        }
    }

    /**
     * @brief Leg E -- an MSAA producer. Its resolve must complete before the consumer samples it,
     *        without any readback to trigger it.
     *
     * The DESTINATION is deliberately single-sampled and is the only thing read, so this leg
     * measures the producer's resolve-before-sample ordering and never the separate question of
     * whether a multisampled target's own first readback works (REMED-GFX-154 on bgfx).
     *
     * Whether the requested sample count is genuinely APPLIED is measured, not assumed. Several
     * renderers (Vulkan among them) derive a render target's sample count from the device's own, so
     * a run without GraphicsDeviceManager.PreferMultiSampling reports 0 here and this leg is an
     * honest sample-count-one case. The `--msaa` run of this same fixture sets that property, which
     * is what makes "one genuinely applied MSAA count" a measured claim rather than a request.
     */
    void LegMsaa(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                         RenderTargetUsage::DiscardContents);
        const int applied = a.getMultiSampleCountProperty();
        std::printf("[INFO] E1 requested MultiSampleCount 4, applied %d (device %s multisampling)\n",
                    applied, preferMultiSampling_ ? "PREFERS" : "does not prefer");
        std::fflush(stdout);
        check(!preferMultiSampling_ || applied > 1,
              "E0 the --msaa run genuinely applies a multisample count on this renderer (applied " +
              std::to_string(applied) + ")");
        ProduceInto(dev, a, patternTex_);

        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(a,           Rectangle(0,   0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.Draw(patternTex_, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW * 2, kPH);
        if (!RequireReadable(r, "E1 MSAA producer -> consumer")) return;
        // The pattern is drawn 1:1 with point sampling and axis-aligned edges, so every sample of
        // every pixel lands inside one source texel and the resolve is exact, not an average.
        CheckAgainstControl(r, 0, kPW,
                            std::string("E1 a producer with applied sample count ") +
                            std::to_string(applied) + " resolves before it is sampled",
                            PatternColor);
    }

    /**
     * @brief Leg F -- a mipmapped producer. Mip generation must happen at unbind, not at readback.
     *
     * Only level 0 is asserted: sampling a generated lower mip is a filtering question this task
     * does not own, and the destination is drawn 1:1 so level 0 is what a correct implementation
     * selects. What matters here is that OWNING a mip chain does not move the producer's content
     * behind a readback.
     */
    void LegMips(GraphicsDevice& dev)
    {
        // Whether a renderer supports a mipmapped render target AT ALL is measured, not assumed:
        // WebGPU documents the chain regeneration unimplemented and throws from the constructor
        // (plans/plan_webgpu.md WEBGPU-53/54). That is a declared capability boundary, not this finding.
        std::unique_ptr<RenderTarget2D> owner;
        try
        {
            owner = std::make_unique<RenderTarget2D>(dev, kPW, kPH, true, SurfaceFormat::Color,
                                                     DepthFormat::None, 0,
                                                     RenderTargetUsage::DiscardContents);
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] F1 mipmapped RenderTarget2D unsupported on %s (%s) -- boundary "
                        "recorded; the sample-count-one legs above carry the contract\n",
                        kRendererName, e.what());
            std::fflush(stdout);
            return;
        }
        RenderTarget2D& a = *owner;
        std::printf("[INFO] F1 mipmapped producer LevelCount=%d\n", a.getLevelCountProperty());
        std::fflush(stdout);
        ProduceInto(dev, a, patternTex_);

        RenderTarget2D dst(dev, kPW * 2, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(a,           Rectangle(0,   0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.Draw(patternTex_, Rectangle(kPW, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWhole(dst, kPW * 2, kPH);
        if (!RequireReadable(r, "F1 mipmapped producer -> consumer")) return;
        CheckAgainstControl(r, 0, kPW, "F1 a mipmapped producer is sampleable at level 0 with no "
                                       "readback", PatternColor);
    }

    /**
     * @brief Leg G -- RenderTargetUsage and ordered Clear. The consumer must see the FINAL producer
     *        content according to exact public order, whatever built it.
     *
     * Four producer shapes are exercised against each usage: clear only, geometry only, clear then
     * geometry, and geometry then clear (REMED-GFX-129's ordered Clear -- the clear runs AFTER the
     * draw and therefore wins).
     */
    void LegUsageAndClear(GraphicsDevice& dev)
    {
        struct Usage { const char* name; RenderTargetUsage usage; };
        const Usage usages[] = {
            { "DiscardContents", RenderTargetUsage::DiscardContents },
            { "PreserveContents", RenderTargetUsage::PreserveContents },
            { "PlatformContents", RenderTargetUsage::PlatformContents },
        };
        const Color clearA(70, 130, 190, 255);
        const Color clearB(200, 60, 110, 255);

        for (const Usage& u : usages)
        {
            // G-a: producer built from Clear() alone.
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 u.usage);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(clearA);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);

                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                ProduceInto(dev, dst, a);
                Readback r = ReadWhole(dst, kPW, kPH);
                if (RequireReadable(r, std::string("G1 ") + u.name + " clear-only producer"))
                {
                    bool all = true;
                    for (const Color& c : r.pixels) if (!Same(c, clearA)) { all = false; break; }
                    check(all, std::string("G1 ") + u.name +
                          ": a producer built from Clear() alone is sampleable with no readback "
                          "(want " + ColorText(clearA) + ", got " + ColorText(r.at(0, 0)) + ")");
                }
            }

            // G-b: Clear() then geometry -- the geometry wins because it comes second.
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 u.usage);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(clearA);
                BlitPattern(dev, patternTex_);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);

                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                ProduceInto(dev, dst, a);
                Readback r = ReadWhole(dst, kPW, kPH);
                if (RequireReadable(r, std::string("G2 ") + u.name + " clear-then-draw producer"))
                    CheckRegion(r, 0, std::string("G2 ") + u.name +
                                ": Clear() then geometry -- the consumer sees the geometry",
                                PatternColor);
            }

            // G-c: geometry then Clear() -- REMED-GFX-129's ordered clear runs AFTER the draw, so
            // the consumer must see the CLEAR, not the pattern. A renderer that could only deliver a
            // clear through the pass load action would show the pattern here.
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 u.usage);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(clearA);
                BlitPattern(dev, patternTex_);
                dev.Clear(clearB);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);

                RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                ProduceInto(dev, dst, a);
                Readback r = ReadWhole(dst, kPW, kPH);
                if (RequireReadable(r, std::string("G3 ") + u.name + " draw-then-clear producer"))
                {
                    bool all = true;
                    for (const Color& c : r.pixels) if (!Same(c, clearB)) { all = false; break; }
                    static_assert(kOrderedClearAfterDrawWins,
                                  "REMED-GFX-156: every renderer this fixture runs on delivers an "
                                  "ordered Clear now; a renderer that regresses must re-introduce "
                                  "the declaration rather than weaken this leg.");
                    check(all, std::string("G3 ") + u.name +
                          ": geometry then Clear() -- the consumer sees the LAST command (want " +
                          ColorText(clearB) + ", got " + ColorText(r.at(0, 0)) + ")");
                }
            }
        }
    }

    /**
     * @brief Leg H -- SpriteBatch knobs on a never-read producer: source rectangle and both filters.
     *
     * LinearClamp is measured at deterministic sample centres only: the destination is 1:1 with the
     * source and axis-aligned, so each pixel centre lands exactly on one texel centre and linear
     * filtering returns that texel byte-exactly, with no tolerance needed.
     */
    void LegSpriteBatchKnobs(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        // H1: LinearClamp at 1:1.
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            BlitPatternAt(dev, a, 0, SamplerState::LinearClamp);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback r = ReadWhole(dst, kPW, kPH);
            if (RequireReadable(r, "H1 LinearClamp consumer"))
                CheckRegion(r, 0, "H1 a never-read producer sampled with LinearClamp at 1:1",
                            PatternColor);
        }

        // H2: a sub-rectangle of the producer.
        const Rectangle sub(4, 2, 4, 2);
        RenderTarget2D dst(dev, 4, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        BlitPatternAt(dev, a, 0, SamplerState::PointClamp, sub);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWhole(dst, 4, 2);
        if (RequireReadable(r, "H2 source-rectangle consumer"))
        {
            int good = 0;
            std::string first;
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 4; ++x)
                {
                    const Color want = PatternColor(sub.X + x, sub.Y + y);
                    if (Same(r.at(x, y), want)) ++good;
                    else if (first.empty())
                        first = " first at (" + std::to_string(x) + "," + std::to_string(y) +
                                ") want=" + ColorText(want) + " got=" + ColorText(r.at(x, y));
                }
            check(good == 8, "H2 a never-read producer sampled through source rectangle "
                             "(4,2,4,2) (" + std::to_string(good) + "/8)" + first);
        }
    }

    /**
     * @brief Leg I -- the backbuffer boundary a mid-frame readback must not disturb.
     *
     * A mid-frame `GetData` of one target forces work to the GPU early. The exact public sequence
     * measured here is the one where "early" could mean "too early" for something else:
     *
     *     bind u; draw A into u; unbind;      // cycle 1
     *     draw u onto the BACKBUFFER;         // cycle 2 -- must see A
     *     bind u; draw B into u; unbind;      // cycle 3
     *     bind t; draw; unbind; t.GetData();  // cycle 4 -- an unrelated target, read mid-frame
     *
     * The backbuffer draw sits between u's two cycles and must still show A, not B, even though
     * the readback of `t` happened after both. This is REMED-GFX-143's ascending-id contract seen
     * from the readback side, and it is asserted here rather than assumed because REMED-GFX-151's
     * fix is what decides how much of the frame a readback replays.
     */
    void LegBackbufferBoundary(GraphicsDevice& dev)
    {
        RenderTarget2D u(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, u, patternTex_);                 // cycle 1: u := pattern

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {                                                 // cycle 2: backbuffer samples u
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(u, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }

        ProduceInto(dev, u, altPatternTex_);              // cycle 3: u := alt pattern

        RenderTarget2D t(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, t, patternTex_);                 // cycle 4: an unrelated target ...
        Readback rt = ReadWhole(t, kPW, kPH);             // ... read mid-frame
        if (RequireReadable(rt, "I1 unrelated mid-frame readback"))
            CheckRegion(rt, 0, "I1 an unrelated target read mid-frame is its own content",
                        PatternColor);

        std::vector<Color> frame(static_cast<std::size_t>(kBBW) * kBBH,
                                 Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool threw = false;
        std::string what;
        try { dev.GetBackBufferData(frame.data(), 0, static_cast<int>(frame.size())); }
        catch (const System::NotSupportedException&) { threw = true; what = "NotSupportedException"; }
        catch (const std::exception& e) { threw = true; what = e.what(); }

        if (threw || Unsupported())
        {
            std::printf("[INFO] I2 backbuffer oracle unavailable on %s (%s) -- boundary recorded\n",
                        kRendererName, threw ? what.c_str() : "non-rasterizing");
            std::fflush(stdout);
            return;
        }
        Readback r;
        r.w = kBBW; r.h = kBBH; r.pixels = frame;
        if (!kBackbufferConsumerSeesNeverReadTarget)
        {
            // REMED-GFX-155 owns the backbuffer consumer on this renderer, so there is no honest
            // ordering to measure here until it is fixed -- recorded, not asserted either way.
            std::printf("[INFO] I2 not measurable on %s: REMED-GFX-155 (a never-read target "
                        "reaching a backbuffer consumer) owns this path; got %s\n",
                        kRendererName, ColorText(r.at(0, 0)).c_str());
            std::fflush(stdout);
            return;
        }
        CheckRegion(r, 0, "I2 a backbuffer draw between two cycles of the same target still shows "
                          "the cycle it was issued after, despite a later mid-frame readback",
                    PatternColor);
    }

#if defined(CNA_RENDERER_VULKAN)
    /**
     * @brief Leg V -- Khronos validation over everything the legs above just did.
     *
     * Classification is by the stable `SYNC-HAZARD` message-id prefix rather than by matching a
     * complete diagnostic sentence, so a layer rewording cannot turn a real hazard green. The
     * layer's own liveness is asserted separately: a zero hazard count from a layer that never
     * loaded is not evidence of anything, and REMED-GFX-144's acquire hazards are called out by
     * name so a regression there is named rather than merely counted.
     */
    void LegValidation()
    {
        auto* vk = dynamic_cast<CNA::Internal::Renderers::Vulkan::VulkanRenderer*>(
            &getGraphicsDeviceProperty().GetRenderer());
        check(CNA::Internal::Renderers::Vulkan::VulkanRenderer::IsValidationActiveEXT(),
              "V1 VK_LAYER_KHRONOS_validation is loaded, so the counts below mean something");
        if (!vk) { check(false, "V1 Vulkan renderer not reachable"); return; }

        const auto& msgs = vk->GetValidationMessagesEXT();
        const auto& ids  = vk->GetValidationMessageIdNamesEXT();
        int sync = 0, acquire = 0, other = 0;
        std::string firstSync, firstOther;
        for (std::size_t i = 0; i < msgs.size(); ++i)
        {
            const std::string& m  = msgs[i];
            const std::string& id = (i < ids.size()) ? ids[i] : std::string();
            if (id.find("SYNC-HAZARD") != std::string::npos ||
                m.find("hazard detected") != std::string::npos)
            {
                ++sync;
                if (m.find("vkAcquireNextImageKHR") != std::string::npos) ++acquire;
                if (firstSync.empty()) firstSync = id + " -- " + m.substr(0, 220);
            }
            else
            {
                ++other;
                if (firstOther.empty()) firstOther = id + " -- " + m.substr(0, 220);
            }
        }
        std::printf("[INFO] V0 sync validation %s; %d validation messages total\n",
                    syncValidation_ ? "REQUESTED" : "not requested",
                    static_cast<int>(msgs.size()));
        std::fflush(stdout);
        check(sync == 0, "V2 zero synchronization hazards across the whole producer/consumer "
                         "matrix" + (sync == 0 ? std::string()
                                               : " -- got " + std::to_string(sync) + " (" +
                                                 std::to_string(acquire) +
                                                 " naming vkAcquireNextImageKHR): " + firstSync));
        check(other == 0, "V3 zero other validation messages" +
                          (other == 0 ? std::string()
                                      : " -- got " + std::to_string(other) + ": " + firstOther));
    }
#endif

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();

        whiteTex_ = Texture2D(dev, 1, 1);
        const Color white = Color::White;
        whiteTex_.SetData(&white, 1);

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
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));

        std::printf("[INFO] REMED-GFX-151 same-frame render-to-texture producer/consumer on %s "
                    "(%dx%d pattern, %s)\n", kRendererName, kPW, kPH,
                    preferMultiSampling_ ? "PreferMultiSampling=true" : "PreferMultiSampling=false");
        std::fflush(stdout);

        LegCanonical(dev);
        LegBarrierDiagnostic(dev);
        Leg3DConsumers(dev);
        LegChains(dev);
        LegMsaa(dev);
        LegMips(dev);
        LegUsageAndClear(dev);
        LegSpriteBatchKnobs(dev);
        LegBackbufferBoundary(dev);
#if defined(CNA_RENDERER_VULKAN)
        LegValidation();
#endif

        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    /**
     * @brief Builds the fixture.
     *
     * @param preferMultiSampling Requests a multisampled device. Renderers that derive a render
     *        target's sample count from the device's own only apply a target MultiSampleCount when
     *        this is set, so the `--msaa` run is what turns leg E into a genuinely multisampled
     *        producer instead of a second sample-count-one case.
     */
    explicit RenderTargetProducerConsumerTest(bool preferMultiSampling, bool syncValidation)
        : preferMultiSampling_(preferMultiSampling), syncValidation_(syncValidation)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (preferMultiSampling) gdm_->setPreferMultiSamplingProperty(true);
    }

    [[nodiscard]] int Result() const { return result_; }

private:
    bool preferMultiSampling_ = false;
    bool syncValidation_      = false;
};

int main(int argc, char** argv)
{
    bool msaa = false;
    bool syncval = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--msaa")    msaa = true;
        if (std::string(argv[i]) == "--syncval") syncval = true;
    }

#if defined(CNA_RENDERER_VULKAN)
    // BEFORE the Game is constructed: the renderer creates its VkInstance during construction, and
    // VkValidationFeaturesEXT can only be supplied there. Asking later silently produces an
    // instance WITHOUT synchronization validation, which would make leg V pass for the wrong
    // reason -- which is why V1 asserts the layer's liveness separately from the counts.
    if (syncval)
        CNA::Internal::Renderers::Vulkan::VulkanRenderer::SetSyncValidationEnabledEXT(true);
#endif

    RenderTargetProducerConsumerTest test(msaa, syncval);
    test.Run();
    return test.Result();
}
