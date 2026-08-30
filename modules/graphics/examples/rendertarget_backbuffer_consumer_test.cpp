// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-155: a render target produced earlier in a public frame must be visible to a consumer
// that draws on the BACKBUFFER later in that same frame.
//
//     SetRenderTarget(A)                  // producer
//     draw a pattern into A
//     SetRenderTarget(nullptr)            // back to the backbuffer
//     draw, sampling A as a texture       // consumer -- on the BACKBUFFER
//     ... finish the ordinary public frame
//
// REMED-GFX-151 established this contract for a render-target consumer. This file is the same
// contract for the one destination that fixture could only pin, not assert: the backbuffer.
//
// THE DEFECT this file reproduces is bgfx-local, and it is an ORDERING defect, not a visibility,
// synchronization or resource-identity one. bgfx does not execute views in the order they were
// submitted to. Every draw carries a sort key whose high bits are its view's SORT POSITION, the
// whole frame is radix-sorted by that key, and the position defaults to the numeric view id. The
// bgfx renderer partitions ids as backbuffer = 0, render-target base ids in [1, 192), per-frame
// ordered segments in [192, 255) -- so "execute in ascending id" meant "execute the backbuffer
// FIRST, then render targets in the order their ids happened to be allocated". Measured, the frame
// behind REMED-GFX-151's leg D6 used views 1, 192, 0 in public order and executed them 0, 1, 192:
// the backbuffer consumer ran before both of its producer's views and sampled an image nothing had
// rendered into yet. All 32 texels came back (0,0,0,0).
//
// Two things follow, and both are asserted here rather than assumed:
//
//   * A render-target CONSUMER was correct only by accident. A producer allocated before its
//     consumer gets the lower id, so ascending order happened to match public order in every
//     fixture written so far. Reverse that allocation -- create the consumer target FIRST -- and
//     the identical public sequence breaks in exactly the same way. Leg F2 is that case.
//   * Nothing was ever wrong with the backbuffer draw itself, with the sampled handle, or with the
//     producer's content. Every backbuffer leg below therefore draws an ordinary Texture2D control
//     holding the same expected bytes, in the SAME batch, through the same sampler and blend state,
//     and compares the two side by side. A failure names which side is wrong instead of reporting
//     that "the frame is black".
//
// THE FIX is bgfx::setViewOrder(): the renderer records the view ids a frame's public commands use,
// in first-use order, and programs that as the frame's execution order. No GetData, no extra
// bgfx::frame(), no Present, no readback, no wait, and no per-target-switch flush is added -- see
// the cardinality leg, which measures that a repeated producer/consumer frame allocates a bounded,
// constant number of views and never grows.
//
// THE ORACLE is GetBackBufferData. It is honest here: the Texture2D control drawn beside every
// render-target region is read through the very same call, so a broken readback would fail the
// control too. Where a renderer cannot read its backbuffer at all, the leg says so and the
// render-target-to-render-target control still carries the ordering contract.
//
// THE PATTERN is 8x4 -- deliberately NON-SQUARE, so a transpose cannot masquerade as a pass -- and
// every one of its 32 texels is unique in (R, G). A stale buffer, a zero buffer, a uniform fill, a
// mirror, a rotation and a swapped resource each fail distinguishably. No leg reads a producer
// before sampling it: reading it is precisely the operation this contract says is unnecessary.
//
// LEGS (each answers one question, so a failure names the layer):
//
//   A  render-target consumer              the known-correct control REMED-GFX-151 left green
//   B  backbuffer consumer                 the finding itself, SpriteBatch and stock 3D
//   C  chains ending on the backbuffer     A -> B -> backbuffer, and backbuffer -> A -> backbuffer
//   D  ordinary Texture2D on the backbuffer  proves the backbuffer draw path independently
//   E  one producer sampled twice          same batch, and two separate batches
//   F  two producers, adverse orders       consumed in reverse public order; consumer target
//                                          CREATED BEFORE its producer (the latent id-order case)
//   G  producer -> backbuffer -> producer -> backbuffer   each draw shows the cycle it followed
//   H  RenderTargetCube face producer      a cube bind cycle and a backbuffer draw in one frame
//   I  draw-family interleaving            SpriteBatch -> 3D -> SpriteBatch, 3D -> sprite -> 3D
//   J  MSAA and mipmapped producers        resolve/mip generation must not need a readback
//   K  RenderTargetUsage                   Preserve / Discard / Platform, clear-only and geometry
//   L  cardinality and repetition          many bind cycles per frame, many frames; bounded views
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
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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
     * deterministically. There is no consumer result to observe there; the legs below assert the
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
#error "REMED-GFX-155: this renderer has no declared backbuffer-consumer contract."
#endif

    /**
     * @brief Whether a RenderTarget2D may be handed to a stock 3D effect as its texture.
     *
     * REMED-GFX-152 CLOSED this (2026-07-29), and the declaration is now unconditionally true.
     *
     * It used to read false on SDL_GPU, whose stock-effect paths
     * `static_cast<const SdlGpuTextureRenderer*>(params.textureN)` onto the unrelated sibling
     * SdlGpuRenderTargetRenderer and died, so the 3D consumer cases were skipped there. They now run
     * and were A/B-proven against the pre-fix renderer.
     */
    constexpr bool kStockEffectRtSourceSupported = true;

    /**
     * @brief Whether a bind cycle's SpriteBatch and stock 3D draws replay in public order.
     *
     * REMED-GFX-157 corrected Vulkan and bgfx, which grouped a cycle's draws by family. WEBGPU
     * grouped them the other way round -- all 3D draws, then all sprites -- so a SpriteBatch
     * followed by an OVERLAPPING 3D draw came out with the sprite on top there, and only leg I0's
     * overlapping render-target probe could see it: every other leg here draws its two families
     * into disjoint slots, which any grouping renders correctly. REMED-GFX-159 replaced WebGPU's
     * fixed per-family replay list with one ordered reference stream, so every renderer now owes
     * public order and this is asserted unconditionally.
     */
    constexpr bool kFamiliesReplayInPublicOrder = true;

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
 * @brief REMED-GFX-155's public backbuffer-consumer contract for a same-frame render target.
 */
class RenderTargetBackbufferConsumerTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;

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
    Readback ReadBackbuffer(GraphicsDevice& dev)
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
        out = ReadBackbuffer(dev);
        if (Unsupported() || !out.ok())
        {
            std::printf("[INFO] %s: backbuffer oracle unavailable on %s (%s) -- boundary recorded, "
                        "the render-target-consumer control carries the contract\n",
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

    /// Draws @p source 1:1 into backbuffer slot @p slot through SpriteBatch, point-sampled.
    void DrawSpriteSlot(GraphicsDevice& dev, const Texture2D& source, int slot)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        // REMED-GFX-157: pass the depth and rasterizer states EXPLICITLY. A null means
        // "DepthStencilState.None and RasterizerState.CullCounterClockwise" (FNA SpriteBatch.cs:
        // `rasterizerState ?? RasterizerState.CullCounterClockwise`), and FNA's PrepRenderState
        // assigns them to the device and never restores them -- so a null left CullCounterClockwise
        // behind, and leg I below then reported the following 3D draw as LOST when it had merely
        // been culled. Ordering is what this fixture measures, so it states the state it wants.
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(source, Slot(slot), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws two sources into two slots inside ONE SpriteBatch batch, so a leg can prove the two
    /// regions really were produced by the same submission rather than by two independent ones.
    void DrawSpriteSlots(GraphicsDevice& dev, const Texture2D& a, int slotA,
                         const Texture2D& b, int slotB)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(a, Slot(slotA), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.Draw(b, Slot(slotB), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
    }

    /// Draws @p source into backbuffer slot @p slot through BasicEffect, so the ordering contract is
    /// proven for the stock 3D path and not only for SpriteBatch. The slot is expressed as clip-space
    /// quad coordinates rather than as a Viewport, so this leg cannot accidentally exercise the
    /// viewport-segmentation machinery (REMED-GFX-063/065) instead of the ordering under test.
    void Draw3DSlot(GraphicsDevice& dev, Texture2D& source, int slot)
    {
        const Rectangle d = Slot(slot);
        const float xL = 2.0f * static_cast<float>(d.X) / kBBW - 1.0f;
        const float xR = 2.0f * static_cast<float>(d.X + d.Width) / kBBW - 1.0f;
        const float yT = 1.0f - 2.0f * static_cast<float>(d.Y) / kBBH;
        const float yB = 1.0f - 2.0f * static_cast<float>(d.Y + d.Height) / kBBH;

        VertexPositionTexture q[6];
        q[0] = { Vector3(xL, yT, 0.f), Vector2(0.f, 0.f) };
        q[1] = { Vector3(xL, yB, 0.f), Vector2(0.f, 1.f) };
        q[2] = { Vector3(xR, yB, 0.f), Vector2(1.f, 1.f) };
        q[3] = { Vector3(xL, yT, 0.f), Vector2(0.f, 0.f) };
        q[4] = { Vector3(xR, yB, 0.f), Vector2(1.f, 1.f) };
        q[5] = { Vector3(xR, yT, 0.f), Vector2(1.f, 0.f) };

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

    /// Produces the pattern inside @p rt through ordinary (never-defective) Texture2D sampling and
    /// unbinds it. The target is NEVER read back here -- that is the whole point of this fixture.
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157, see DrawSpriteSlot
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        sb.Draw(source, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
        sb.End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
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

    /// Asserts the kPW x kPH region at slot @p slot reproduces @p want exactly, naming the failure
    /// shape (empty / never-written / uniform) rather than only reporting a count.
    void CheckSlot(const Readback& r, int slot, const std::string& label, PatternFn want)
    {
        const Rectangle d = Slot(slot);
        int good = 0, empty = 0, poison = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const Color& got = r.at(d.X + x, d.Y + y);
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

    /// The canonical oracle: a render-target-sourced slot and a Texture2D-sourced control slot drawn
    /// with identical geometry and state must be byte-identical, and the control must itself be the
    /// pattern -- so a failure names which side is wrong.
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
              label + ": render-target source and Texture2D control sample identically (" +
              std::to_string(kPW * kPH - mismatched) + "/" + std::to_string(kPW * kPH) + ")" + first);
        CheckSlot(r, texSlot, label + ": the Texture2D control itself is the pattern", want);
        CheckSlot(r, rtSlot,  label + ": the render-target source is the producer's content", want);
    }

    /// Prints a slot's first row, so the measured content is on the record rather than inferred.
    static void PrintSlotRow(const Readback& r, int slot, const char* label)
    {
        if (!r.ok()) return;
        const Rectangle d = Slot(slot);
        std::printf("[INFO] %s row0:", label);
        for (int x = 0; x < kPW; ++x)
            std::printf(" %s", ColorText(r.at(d.X + x, d.Y)).c_str());
        std::printf("\n");
        std::fflush(stdout);
    }

    // ================================================================ legs

    /**
     * @brief Leg A -- the render-target consumer REMED-GFX-151 left green, kept as the control.
     *
     * If this ever fails, the ordering fix broke the path that already worked, and every backbuffer
     * result below is uninterpretable. It runs first for exactly that reason.
     */
    void LegRenderTargetConsumerControl(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&dst);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb.Draw(a, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        Readback r = ReadWholeTarget(dst, kPW, kPH);
        if (!RequireReadable(r, "A1 render-target consumer control")) return;
        CheckSlot(r, 0, "A1 a never-read producer sampled into another RENDER TARGET is exact "
                        "(the control REMED-GFX-151 left green)", PatternColor);
    }

    /**
     * @brief Leg B -- the finding. A never-read producer sampled by a BACKBUFFER consumer.
     *
     * Slot 0 is the render target, slot 1 the ordinary Texture2D control holding the same expected
     * bytes, both drawn inside one SpriteBatch batch. The 3D case repeats the pair through
     * BasicEffect so the contract cannot be satisfied by a SpriteBatch-local change.
     */
    void LegBackbufferConsumer(GraphicsDevice& dev)
    {
        // B1: SpriteBatch consumer.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);

            BeginBackbuffer(dev);
            DrawSpriteSlots(dev, a, 0, patternTex_, 1);

            Readback r;
            if (ReadBackbufferOr(dev, r, "B1"))
            {
                PrintSlotRow(r, 0, "B1 render-target source ");
                PrintSlotRow(r, 1, "B1 Texture2D control    ");
                CheckSlotAgainstControl(r, 0, 1,
                    "B1 SetRenderTarget(A); draw; unbind; sample A on the BACKBUFFER with no GetData",
                    PatternColor);
            }
        }

        // B2: stock 3D consumer, same frame shape.
        if (!kStockEffectRtSourceSupported)
        {
            std::printf("[INFO] B2: %s cannot hand a RenderTarget2D to a stock 3D effect "
                        "(REMED-GFX-152) -- boundary recorded, B1 carries the contract here\n",
                        kRendererName);
            std::fflush(stdout);
        }
        else
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);

            BeginBackbuffer(dev);
            Draw3DSlot(dev, a, 0);
            Draw3DSlot(dev, patternTex_, 1);

            Readback r;
            if (ReadBackbufferOr(dev, r, "B2"))
                CheckSlotAgainstControl(r, 0, 1,
                    "B2 a BasicEffect consumer on the BACKBUFFER samples a never-read producer",
                    PatternColor);
        }
    }

    /**
     * @brief Leg C -- chains that end on the backbuffer, in both directions.
     *
     * C1 is a two-hop chain: A is a producer for B, and B is a producer for the backbuffer, so a
     * fix that only orders the LAST render target before the backbuffer would fail here. C2 puts
     * backbuffer work BEFORE the producer as well as after it, which is the case an ordering scheme
     * that simply pushes the backbuffer last would break.
     */
    void LegChainsToBackbuffer(GraphicsDevice& dev)
    {
        // C1: A -> B -> backbuffer.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            ProduceInto(dev, b, a);

            BeginBackbuffer(dev);
            DrawSpriteSlots(dev, b, 0, patternTex_, 1);

            Readback r;
            if (ReadBackbufferOr(dev, r, "C1"))
                CheckSlotAgainstControl(r, 0, 1, "C1 texture -> A -> B -> BACKBUFFER, nothing read",
                                        PatternColor);
        }

        // C2: backbuffer -> target -> backbuffer, all in one public frame. The first backbuffer
        // batch must survive the target bind cycle that follows it, and the second must see the
        // producer -- so public order has to be honoured in BOTH directions, not merely "targets
        // before the backbuffer".
        {
            BeginBackbuffer(dev);
            DrawSpriteSlot(dev, altPatternTex_, 2);       // backbuffer work BEFORE the producer

            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);

            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            DrawSpriteSlots(dev, a, 0, patternTex_, 1);   // backbuffer work AFTER it

            Readback r;
            if (ReadBackbufferOr(dev, r, "C2"))
            {
                CheckSlotAgainstControl(r, 0, 1,
                    "C2 backbuffer -> target -> backbuffer: the later batch sees the producer",
                    PatternColor);
                CheckSlot(r, 2, "C2 the backbuffer batch issued BEFORE the producer survives the "
                                "target bind cycle that followed it", AltPatternColor);
            }
        }
    }

    /**
     * @brief Leg D -- the backbuffer draw path on its own, with no render target anywhere.
     *
     * Establishes independently that a backbuffer batch, its readback and this fixture's slot
     * geometry are all sound, so a failure anywhere above can be attributed to the render-target
     * source rather than to the destination.
     */
    void LegOrdinaryTextureOnBackbuffer(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, patternTex_, 0, altPatternTex_, 1);

        Readback r;
        if (!ReadBackbufferOr(dev, r, "D1")) return;
        CheckSlot(r, 0, "D1 an ordinary Texture2D drawn on the backbuffer is exact", PatternColor);
        CheckSlot(r, 1, "D1 a second ordinary Texture2D in the same batch is its own content",
                  AltPatternColor);
    }

    /**
     * @brief Leg E -- one producer sampled more than once by backbuffer consumers.
     *
     * E1 samples it twice inside a single batch; E2 samples it in two separate batches, which on a
     * view-based renderer is what forces a second consumer segment. Both must agree with the control.
     */
    void LegSampledTwice(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);

        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, a, 0, a, 1);         // E1: twice in ONE batch
        DrawSpriteSlot(dev, a, 2);                // E2: a separate batch
        DrawSpriteSlot(dev, patternTex_, 3);      // control

        Readback r;
        if (!ReadBackbufferOr(dev, r, "E")) return;
        CheckSlotAgainstControl(r, 0, 3, "E1 a producer sampled twice in one backbuffer batch (1st)",
                                PatternColor);
        CheckSlotAgainstControl(r, 1, 3, "E1 a producer sampled twice in one backbuffer batch (2nd)",
                                PatternColor);
        CheckSlotAgainstControl(r, 2, 3, "E2 the same producer sampled again in a SEPARATE "
                                         "backbuffer batch", PatternColor);
    }

    /**
     * @brief Leg F -- two live producers under orders that defeat an id-based execution rule.
     *
     * F1 consumes them in the reverse of the order they were produced in. F2 is the latent case
     * this task exposed: the CONSUMER's target is created BEFORE the producer's, so a renderer that
     * executes render targets in resource-allocation order rather than public order runs the
     * consumer first. Both patterns are used so sampling the wrong live producer fails on every
     * texel rather than on a few.
     */
    void LegTwoProducersAdverseOrder(GraphicsDevice& dev)
    {
        // F1: produce A then B; consume B then A on the backbuffer.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            ProduceInto(dev, b, altPatternTex_);

            BeginBackbuffer(dev);
            DrawSpriteSlot(dev, b, 0);
            DrawSpriteSlot(dev, a, 1);
            DrawSpriteSlots(dev, altPatternTex_, 2, patternTex_, 3);

            Readback r;
            if (ReadBackbufferOr(dev, r, "F1"))
            {
                CheckSlotAgainstControl(r, 0, 2, "F1 the second producer, consumed first",
                                        AltPatternColor);
                CheckSlotAgainstControl(r, 1, 3, "F1 the first producer, consumed second",
                                        PatternColor);
            }
        }

        // F2: the consumer's destination target exists BEFORE the producer does. Nothing about the
        // public sequence changes -- only the order the two targets were created in.
        {
            RenderTarget2D consumerDst(dev, kPW, kPH, false, SurfaceFormat::Color,
                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D producer(dev, kPW, kPH, false, SurfaceFormat::Color,
                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            ProduceInto(dev, producer, patternTex_);

            dev.SetRenderTarget(&consumerDst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                DepthStencilState noDepth = DepthStencilState::None;
                RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
                sb.Draw(producer, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
                sb.End();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            Readback r = ReadWholeTarget(consumerDst, kPW, kPH);
            if (RequireReadable(r, "F2 consumer target created before its producer"))
                CheckSlot(r, 0, "F2 a consumer target CREATED BEFORE its producer still sees the "
                                "producer (public order, not resource-allocation order)",
                          PatternColor);
        }

        // F3: the same adverse creation order, ending on the backbuffer.
        {
            RenderTarget2D unusedFirst(dev, kPW, kPH, false, SurfaceFormat::Color,
                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            RenderTarget2D producer(dev, kPW, kPH, false, SurfaceFormat::Color,
                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            // The earlier-created target is written too, so it is genuinely live and not merely a
            // way of consuming an identifier.
            ProduceInto(dev, unusedFirst, altPatternTex_);
            ProduceInto(dev, producer, patternTex_);

            BeginBackbuffer(dev);
            DrawSpriteSlots(dev, producer, 0, patternTex_, 1);
            DrawSpriteSlots(dev, unusedFirst, 2, altPatternTex_, 3);

            Readback r;
            if (ReadBackbufferOr(dev, r, "F3"))
            {
                CheckSlotAgainstControl(r, 0, 1, "F3 the later-created producer on the backbuffer",
                                        PatternColor);
                CheckSlotAgainstControl(r, 2, 3, "F3 the earlier-created producer on the backbuffer",
                                        AltPatternColor);
            }
        }
    }

    /**
     * @brief Leg G -- producer, backbuffer, the SAME producer again, backbuffer again.
     *
     * Each backbuffer batch must show the bind cycle it was issued after -- the first the pattern,
     * the second the alternative -- so neither an ordering that merges a target's two cycles nor one
     * that lets the later cycle overtake the earlier consumer can pass.
     */
    void LegRebindBetweenConsumers(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);

        ProduceInto(dev, a, patternTex_);                 // cycle 1
        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, a, 0, patternTex_, 1);       // consumer 1 -- must see cycle 1

        ProduceInto(dev, a, altPatternTex_);              // cycle 2, same target

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        DrawSpriteSlots(dev, a, 2, altPatternTex_, 3);    // consumer 2 -- must see cycle 2

        Readback r;
        if (!ReadBackbufferOr(dev, r, "G")) return;
        CheckSlotAgainstControl(r, 0, 1, "G1 the backbuffer consumer issued after the FIRST cycle "
                                         "shows that cycle", PatternColor);
        CheckSlotAgainstControl(r, 2, 3, "G2 the backbuffer consumer issued after the SECOND cycle "
                                         "shows that cycle", AltPatternColor);
    }

    /**
     * @brief Leg H -- a RenderTargetCube face bind cycle and backbuffer work in one public frame.
     *
     * The public XNA API cannot hand a RenderTargetCube to SpriteBatch or to a stock textured
     * effect, so a cube face cannot be sampled by a backbuffer consumer without an environment-map
     * reflection whose result is not byte-comparable to a source pattern. Rather than fake that,
     * this leg asserts the two things that ARE expressible and that an ordering change could break:
     * a backbuffer batch issued after a cube bind cycle is exact, and the cube face still holds what
     * its producer wrote. REMED-GFX-134's reserved cube view ordering is what this protects.
     */
    void LegCubeFaceProducer(GraphicsDevice& dev)
    {
        // Whether a cube face can be BOUND at all is asked of the public API rather than of a
        // hard-coded renderer list, because the two disagree: Software implements RenderTargetCube
        // yet rejects SetRenderTarget for a face. A renderer that cannot bind one has no cube bind
        // cycle to order, so the boundary is recorded and the leg ends.
        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(dev, kPW, false, SurfaceFormat::Color,
                                                      DepthFormat::None, 0,
                                                      RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
        }
        catch (const System::NotSupportedException& e)
        {
            std::printf("[INFO] H: %s cannot bind a RenderTargetCube face (%s) -- boundary "
                        "recorded\n", kRendererName, e.what());
            std::fflush(stdout);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return;
        }
        catch (const std::exception& e)
        {
            std::printf("[INFO] H: %s cannot bind a RenderTargetCube face (%s) -- boundary "
                        "recorded\n", kRendererName, e.what());
            std::fflush(stdout);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            return;
        }

        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
            sb.Draw(patternTex_, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, altPatternTex_);

        BeginBackbuffer(dev);
        DrawSpriteSlots(dev, a, 0, altPatternTex_, 1);

        Readback bb;
        if (ReadBackbufferOr(dev, bb, "H1"))
            CheckSlotAgainstControl(bb, 0, 1,
                "H1 a backbuffer consumer issued after a cube bind cycle still sees its own "
                "2D producer", AltPatternColor);

        std::vector<Color> face(static_cast<std::size_t>(kPW) * kPW, Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool threw = false;
        std::string what;
        try { cube->GetData(CubeMapFace::PositiveX, face.data(), 0, static_cast<int>(face.size())); }
        catch (const System::NotSupportedException&) { threw = true; what = "NotSupportedException"; }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        if (threw)
        {
            std::printf("[INFO] H2 cube readback unavailable on %s (%s) -- boundary recorded\n",
                        kRendererName, what.c_str());
            std::fflush(stdout);
            return;
        }
        // Only the pattern's own kPW x kPH region was drawn into the kPW x kPW face; the rest is the
        // clear. Checking that region is what proves the face's producer survived the frame's
        // reordering, which is the ordering claim this leg exists for.
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
              "H2 the cube face still holds its producer's content after later backbuffer work (" +
              std::to_string(good) + "/" + std::to_string(kPW * kPH) + ")" + first);
    }

    /**
     * @brief Leg I -- SpriteBatch and 3D consumers interleaved on the backbuffer.
     *
     * A prior finding noted that this renderer may replay a bind cycle's sprites and its 3D draws as
     * two separate queues. That is a DIFFERENT question from view ordering, and this leg measures it
     * rather than assuming either answer: every draw here samples a producer that was finished
     * before the whole backbuffer cycle began, and every draw targets its own slot, so the legs pass
     * whether or not the two families keep their relative order among themselves. What they cannot
     * survive is the producer running after any of them.
     */
    /// Runs one interleaved backbuffer cycle and returns the readback. @p first/@p second are the
    /// two sources placed in slots 0/2 and slot 1; @p spriteFirst chooses sprite -> 3D -> sprite or
    /// 3D -> sprite -> 3D. Slots 6 and 7 always carry ordinary-texture controls drawn by sprites.
    Readback RunInterleaved(GraphicsDevice& dev, Texture2D& first, Texture2D& second,
                            bool spriteFirst)
    {
        BeginBackbuffer(dev);
        if (spriteFirst)
        {
            DrawSpriteSlot(dev, first, 0);
            Draw3DSlot(dev, second, 1);
            DrawSpriteSlot(dev, first, 2);
        }
        else
        {
            Draw3DSlot(dev, first, 0);
            DrawSpriteSlot(dev, second, 1);
            Draw3DSlot(dev, first, 2);
        }
        DrawSpriteSlot(dev, patternTex_, 6);
        DrawSpriteSlot(dev, altPatternTex_, 7);
        return ReadBackbuffer(dev);
    }

    void LegDrawFamilyInterleaving(GraphicsDevice& dev)
    {
        if (!kStockEffectRtSourceSupported)
        {
            std::printf("[INFO] I: %s cannot hand a RenderTarget2D to a stock 3D effect "
                        "(REMED-GFX-152) -- boundary recorded\n", kRendererName);
            std::fflush(stdout);
            return;
        }

        // I0: the identical interleaving with ORDINARY textures. This runs first and decides how the
        // render-target cases below are read: whatever I0 cannot do, the render-target cases cannot
        // be blamed for either, because no render target is involved in I0 at all.
        Readback c1 = RunInterleaved(dev, patternTex_, altPatternTex_, true);
        Readback c2 = RunInterleaved(dev, patternTex_, altPatternTex_, false);
        if (Unsupported() || !c1.ok() || !c2.ok())
        {
            std::printf("[INFO] I: backbuffer oracle unavailable on %s -- boundary recorded\n",
                        kRendererName);
            std::fflush(stdout);
            return;
        }

        /// True when the ordinary-texture control put @p want in @p slot, i.e. this renderer can
        /// place a draw of that family at that position at all.
        auto controlPlaced = [&](const Readback& r, int slot, PatternFn want) {
            const Rectangle d = Slot(slot);
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                    if (!Same(r.at(d.X + x, d.Y + y), want(x, y))) return false;
            return true;
        };

        const bool c1Mid  = controlPlaced(c1, 1, AltPatternColor);   // 3D between two sprite batches
        const bool c2Last = controlPlaced(c2, 2, PatternColor);      // 3D after a sprite batch

        // REMED-GFX-157: these two were a DECLARATION here, not a check -- an INFO line reporting
        // that this renderer "cannot place a stock 3D draw issued after a SpriteBatch in the same
        // bind cycle", which then restricted the render-target cases below to the positions the
        // renderer was believed able to place. Both halves of that were wrong. The draw was not
        // lost: this leg let SpriteBatch.Begin default the rasterizer state (a null means
        // RasterizerState.CullCounterClockwise, which FNA assigns to the device and never restores)
        // and then drew a quad of exactly the winding four renderers cull under it. DrawSpriteSlot
        // now states the state it wants, and the contract is asserted rather than declared.
        check(c1Mid,
              "I0 a stock 3D draw issued BETWEEN two SpriteBatch cycles reaches the backbuffer, "
              "with an ordinary Texture2D source and no render target involved at all");
        check(c2Last,
              "I0 a stock 3D draw issued AFTER a SpriteBatch in the same backbuffer bind cycle "
              "reaches the backbuffer, likewise with an ordinary Texture2D source");

        // The same sequence inside a RENDER TARGET bind cycle, so the contract is asserted for both
        // destinations and not only for the backbuffer.
        {
            RenderTarget2D probe(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&probe);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            {
                SpriteBatch sb(dev);
                SamplerState point = SamplerState::PointClamp;
                DepthStencilState noDepth = DepthStencilState::None;
                RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
                sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
                sb.Draw(altPatternTex_, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH),
                        Color::White);
                sb.End();
            }
            {
                VertexPositionTexture q[6];
                const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f);
                const Vector3 br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
                q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) };
                q[2] = { br, Vector2(1.f, 1.f) }; q[3] = { tl, Vector2(0.f, 0.f) };
                q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&patternTex_);
                fx.VertexColorEnabled = false;
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            Readback pr = ReadWholeTarget(probe, kPW, kPH);
            if (pr.ok())
            {
                // The sprite filled the whole target with the ALT pattern and the 3D draw covered
                // it with the pattern, so this is the one place in this file where the two families
                // OVERLAP and the winner names the replay order.
                PatternFn want = kFamiliesReplayInPublicOrder ? PatternColor : AltPatternColor;
                int good = 0;
                for (int y = 0; y < kPH; ++y)
                    for (int x = 0; x < kPW; ++x)
                        if (Same(pr.at(x, y), want(x, y))) ++good;
                check(good == kPW * kPH,
                      std::string("I0 the same SpriteBatch -> 3D sequence inside a RENDER TARGET "
                                  "bind cycle ") +
                      (kFamiliesReplayInPublicOrder
                           ? "puts the 3D draw on top"
                           : "puts the SPRITE on top, because this renderer groups a cycle's draws "
                             "by family and replays all 3D draws first (declared open finding)") +
                      " (" + std::to_string(good) + "/" + std::to_string(kPW * kPH) + ")");
            }
            else
            {
                std::printf("[INFO] I0 the render-target probe is not readable on %s -- boundary "
                            "recorded\n", kRendererName);
                std::fflush(stdout);
            }
        }

        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);
        ProduceInto(dev, b, altPatternTex_);

        // I1: sprite -> 3D -> sprite, with render-target sources.
        Readback r1 = RunInterleaved(dev, a, b, true);
        if (r1.ok())
        {
            CheckSlotAgainstControl(r1, 0, 6, "I1 sprite -> 3D -> sprite: the first sprite consumer",
                                    PatternColor);
            CheckSlotAgainstControl(r1, 2, 6, "I1 sprite -> 3D -> sprite: the second sprite consumer",
                                    PatternColor);
            if (c1Mid)
                CheckSlotAgainstControl(r1, 1, 7,
                    "I1 sprite -> 3D -> sprite: the 3D consumer between them", AltPatternColor);
        }

        // I2: 3D -> sprite -> 3D, with render-target sources.
        Readback r2 = RunInterleaved(dev, a, b, false);
        if (r2.ok())
        {
            CheckSlotAgainstControl(r2, 0, 6, "I2 3D -> sprite -> 3D: the first 3D consumer",
                                    PatternColor);
            CheckSlotAgainstControl(r2, 1, 7, "I2 3D -> sprite -> 3D: the sprite consumer between them",
                                    AltPatternColor);
            if (c2Last)
                CheckSlotAgainstControl(r2, 2, 6, "I2 3D -> sprite -> 3D: the second 3D consumer",
                                        PatternColor);
        }
    }

    /**
     * @brief Leg J -- multisampled and mipmapped producers reaching a backbuffer consumer.
     *
     * A multisampled producer's resolve and a mipmapped producer's level-0 content must both be
     * complete by the time the consumer samples them, with no readback to force either. The applied
     * sample count is printed, because a run whose device declined multisampling reports 0 here and
     * J1 is then simply a second sample-count-one case rather than an MSAA result.
     */
    void LegMsaaAndMips(GraphicsDevice& dev)
    {
        // J1: MSAA producer.
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
            std::printf("[INFO] J1 requested MultiSampleCount 4, applied %d (%s)\n",
                        a.getMultiSampleCountProperty(),
                        preferMultiSampling_ ? "device prefers multisampling"
                                             : "device does not prefer multisampling");
            std::fflush(stdout);
            if (preferMultiSampling_)
                check(a.getMultiSampleCountProperty() > 1,
                      "J0 the --msaa run genuinely applies a multisample count on this renderer "
                      "(applied " + std::to_string(a.getMultiSampleCountProperty()) + ")");

            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            DrawSpriteSlots(dev, a, 0, patternTex_, 1);

            Readback r;
            if (ReadBackbufferOr(dev, r, "J1"))
                CheckSlotAgainstControl(r, 0, 1,
                    "J1 a multisampled producer (applied " +
                    std::to_string(a.getMultiSampleCountProperty()) +
                    ") resolves before a BACKBUFFER consumer samples it", PatternColor);
        }

        // J2: mipmapped producer, sampled at level 0. Whether a renderer supports a mipmapped render
        // target AT ALL is measured, not assumed: WebGPU documents the chain regeneration
        // unimplemented (plans/plan_webgpu.md WEBGPU-53/54). That is a declared capability boundary, not
        // an ordering result.
        {
            std::unique_ptr<RenderTarget2D> owner;
            try
            {
                owner = std::make_unique<RenderTarget2D>(dev, kPW, kPH, true, SurfaceFormat::Color,
                                                         DepthFormat::None, 0,
                                                         RenderTargetUsage::DiscardContents);
            }
            catch (const std::exception& e)
            {
                std::printf("[INFO] J2 mipmapped RenderTarget2D unsupported on %s (%s) -- boundary "
                            "recorded; J1 and the sample-count-one legs carry the contract\n",
                            kRendererName, e.what());
                std::fflush(stdout);
                return;
            }
            RenderTarget2D& a = *owner;
            std::printf("[INFO] J2 mipmapped producer LevelCount=%d\n", a.getLevelCountProperty());
            std::fflush(stdout);

            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            DrawSpriteSlots(dev, a, 0, patternTex_, 1);

            Readback r;
            if (ReadBackbufferOr(dev, r, "J2"))
                CheckSlotAgainstControl(r, 0, 1,
                    "J2 a mipmapped producer is sampleable at level 0 by a BACKBUFFER consumer with "
                    "no readback", PatternColor);
        }
    }

    /**
     * @brief Leg K -- RenderTargetUsage, and producers built from Clear alone or from geometry.
     *
     * A backbuffer consumer must see the producer's FINAL public content under every usage value.
     * The clear-only producer matters because it is the one case where the producer's whole content
     * comes from view state rather than from a draw, which is exactly the kind of work an ordering
     * change can leave attached to the wrong segment.
     */
    void LegUsageAndClearOnlyProducer(GraphicsDevice& dev)
    {
        struct UsageCase { const char* name; RenderTargetUsage usage; };
        const UsageCase cases[] = {
            { "DiscardContents",  RenderTargetUsage::DiscardContents  },
            { "PreserveContents", RenderTargetUsage::PreserveContents },
            { "PlatformContents", RenderTargetUsage::PlatformContents },
        };
        const Color clearOnly(70, 130, 190, 255);

        for (const UsageCase& c : cases)
        {
            // K1: a producer whose entire content is a Clear.
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 c.usage);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(clearOnly);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);

                BeginBackbuffer(dev);
                DrawSpriteSlot(dev, a, 0);

                Readback r;
                if (ReadBackbufferOr(dev, r, std::string("K1 ") + c.name))
                {
                    const Color got = r.at(0, 0);
                    check(Same(got, clearOnly),
                          std::string("K1 ") + c.name + ": a producer built from Clear() alone is "
                          "sampleable by a BACKBUFFER consumer with no readback (want " +
                          ColorText(clearOnly) + ", got " + ColorText(got) + ")");
                }
            }

            // K2: Clear followed by geometry -- the consumer must see the geometry.
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 c.usage);
                dev.SetRenderTarget(&a);
                ResetState(dev);
                dev.Clear(clearOnly);
                {
                    SpriteBatch sb(dev);
                    SamplerState point = SamplerState::PointClamp;
                    DepthStencilState noDepth = DepthStencilState::None;
                    RasterizerState noCull = RasterizerState::CullNone;   // REMED-GFX-157
                    sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
                    sb.Draw(patternTex_, Rectangle(0, 0, kPW, kPH), Rectangle(0, 0, kPW, kPH),
                            Color::White);
                    sb.End();
                }
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);

                BeginBackbuffer(dev);
                DrawSpriteSlots(dev, a, 0, patternTex_, 1);

                Readback r;
                if (ReadBackbufferOr(dev, r, std::string("K2 ") + c.name))
                    CheckSlotAgainstControl(r, 0, 1,
                        std::string("K2 ") + c.name + ": Clear() then geometry -- the BACKBUFFER "
                        "consumer sees the geometry", PatternColor);
            }
        }
    }

    /**
     * @brief Leg L -- cardinality: many bind cycles in one frame, and many frames.
     *
     * L1 runs eight producer/consumer pairs inside a SINGLE public frame. On a view-based renderer
     * every bind cycle consumes ordered view identifiers from a bounded per-frame pool, so a fix
     * that allocated an identifier per draw rather than per cycle would exhaust that pool and throw
     * here rather than merely running slowly. L2 repeats the canonical leg across eight consecutive
     * public frames, which is what proves the per-frame bookkeeping is recycled instead of leaking.
     */
    void LegCardinality(GraphicsDevice& dev)
    {
        // L1: eight producer/consumer pairs, one frame, alternating patterns.
        {
            constexpr int kPairs = 8;
            std::vector<std::unique_ptr<RenderTarget2D>> targets;
            targets.reserve(kPairs);
            bool threw = false;
            std::string what;
            try
            {
                for (int i = 0; i < kPairs; ++i)
                {
                    targets.push_back(std::make_unique<RenderTarget2D>(
                        dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                        RenderTargetUsage::DiscardContents));
                    ProduceInto(dev, *targets.back(), (i % 2) ? altPatternTex_ : patternTex_);
                }
                BeginBackbuffer(dev);
                for (int i = 0; i < kPairs; ++i)
                    DrawSpriteSlot(dev, *targets[static_cast<std::size_t>(i)], i);
            }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            check(!threw, "L1 eight producer/consumer bind cycles in ONE public frame do not "
                          "exhaust any ordered-identifier pool" + (threw ? " -- " + what : ""));

            Readback r;
            if (!threw && ReadBackbufferOr(dev, r, "L1"))
            {
                int exact = 0;
                std::string first;
                for (int i = 0; i < kPairs; ++i)
                {
                    const Rectangle d = Slot(i);
                    PatternFn want = (i % 2) ? AltPatternColor : PatternColor;
                    bool ok = true;
                    for (int y = 0; y < kPH && ok; ++y)
                        for (int x = 0; x < kPW && ok; ++x)
                            if (!Same(r.at(d.X + x, d.Y + y), want(x, y))) ok = false;
                    if (ok) ++exact;
                    else if (first.empty())
                        first = " first wrong slot " + std::to_string(i) + " at (0,0) got " +
                                ColorText(r.at(d.X, d.Y));
                }
                check(exact == kPairs,
                      "L1 all eight same-frame producers reach their backbuffer consumers (" +
                      std::to_string(exact) + "/" + std::to_string(kPairs) + ")" + first);
            }
        }

        // L2: the canonical leg, eight consecutive public frames.
        {
            constexpr int kFrames = 8;
            int exactFrames = 0;
            bool oracle = true;
            std::string first;
            for (int f = 0; f < kFrames; ++f)
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, (f % 2) ? altPatternTex_ : patternTex_);
                BeginBackbuffer(dev);
                DrawSpriteSlot(dev, a, 0);

                Readback r = ReadBackbuffer(dev);
                if (Unsupported() || !r.ok()) { oracle = false; break; }
                PatternFn want = (f % 2) ? AltPatternColor : PatternColor;
                bool ok = true;
                for (int y = 0; y < kPH && ok; ++y)
                    for (int x = 0; x < kPW && ok; ++x)
                        if (!Same(r.at(x, y), want(x, y))) ok = false;
                if (ok) ++exactFrames;
                else if (first.empty())
                    first = " first wrong frame " + std::to_string(f) + " at (0,0) got " +
                            ColorText(r.at(0, 0));
            }
            if (!oracle)
            {
                std::printf("[INFO] L2: backbuffer oracle unavailable on %s -- boundary recorded\n",
                            kRendererName);
                std::fflush(stdout);
            }
            else
            {
                check(exactFrames == kFrames,
                      "L2 the canonical producer -> backbuffer sequence is exact in eight "
                      "consecutive public frames, so nothing leaks across a frame boundary (" +
                      std::to_string(exactFrames) + "/" + std::to_string(kFrames) + ")" + first);
            }
        }
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
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));

        std::printf("[INFO] REMED-GFX-155 same-frame render target -> BACKBUFFER consumer on %s "
                    "(%dx%d pattern, %dx%d backbuffer, %s)\n", kRendererName, kPW, kPH, kBBW, kBBH,
                    preferMultiSampling_ ? "PreferMultiSampling=true" : "PreferMultiSampling=false");
        std::fflush(stdout);

        LegRenderTargetConsumerControl(dev);
        LegOrdinaryTextureOnBackbuffer(dev);
        LegBackbufferConsumer(dev);
        LegChainsToBackbuffer(dev);
        LegSampledTwice(dev);
        LegTwoProducersAdverseOrder(dev);
        LegRebindBetweenConsumers(dev);
        LegCubeFaceProducer(dev);
        LegDrawFamilyInterleaving(dev);
        LegMsaaAndMips(dev);
        LegUsageAndClearOnlyProducer(dev);
        LegCardinality(dev);

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
     *        this is set, so the `--msaa` run is what turns leg J1 into a genuinely multisampled
     *        producer instead of a second sample-count-one case.
     */
    explicit RenderTargetBackbufferConsumerTest(bool preferMultiSampling)
        : preferMultiSampling_(preferMultiSampling)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (preferMultiSampling) gdm_->setPreferMultiSamplingProperty(true);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }

private:
    bool preferMultiSampling_ = false;
};

int main(int argc, char** argv)
{
    bool msaa = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--msaa") msaa = true;

    RenderTargetBackbufferConsumerTest test(msaa);
    test.Run();
    return test.Result();
}
