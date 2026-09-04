// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-167: a resource sampled by a draw that has only been QUEUED must stay bindable until
// that draw actually renders — and destroying the public wrapper first must never take the process
// down.
//
//     RenderTarget2D a(dev, ...);          // producer
//     draw a pattern into a
//     SetRenderTarget(nullptr)             // destination = BACKBUFFER
//     effect.Texture = a; DrawUserPrimitives(...)   // consumer -- only QUEUED here
//     }                                    // a is destroyed, draw still not rendered
//                                          // ... Present() replays it
//
// THE DEFECT this file reproduces is WebGPU-local and it is a LIFETIME defect. Every deferred
// command stored the bound texture as a raw `const IWebGPUSamplable*` — a pointer to the resource's
// RENDERER OBJECT — under this justification, written in the header next to the field:
//
//     "a bound Texture2D's WebGPUTextureRenderer is owned by long-lived game/content state
//      (unlike DrawUserPrimitives' transient vertex buffers), so it is guaranteed to still be
//      alive when this command actually renders later in the same frame."
//
// Nothing guarantees that. `IssueTexturedDraw()` then called `command.texture->View()` at replay —
// a VIRTUAL call through a freed pointer. Measured (`cmake-build-webgpu-asan-ubsan`, `DISPLAY=:101`):
//
//     ERROR: AddressSanitizer: heap-use-after-free on address 0x508000068528
//       #0 WebGPURenderer::IssueTexturedDraw(...)   WebGPURenderer.cpp:7228
//       #1 WebGPURenderer::ReplayDrawsInOrder(...)  WebGPURenderer.cpp:5679
//       #2 WebGPURenderer::ReplayOrderedSegments(...)
//       #3 WebGPURenderer::EnsureFrameRendered()
//       #4 WebGPURenderer::Present()
//     freed by thread T0 here:
//       #1 WebGPURenderTargetRenderer::~WebGPURenderTargetRenderer()  WebGPURenderer.cpp:1746
//       #9 RenderTarget2D::~RenderTarget2D()
//
// and, without a sanitizer, SIGSEGV (exit 139) inside `ReplayDrawsInOrder` with a program counter
// in no mapped module — the freed object's vtable slot. It is NOT a teardown or shutdown defect,
// which is how it first presented: the crash is on the MAIN thread inside the very next `Present()`,
// after the leg's own checks have already printed.
//
// WHAT IS ACTUALLY LOAD-BEARING is not "the backbuffer". It is that NOTHING FLUSHED between the
// consumer draw and the source's destruction. WebGPU renders a render-target destination at
// `SetRenderTarget()`, but a BACKBUFFER destination is not replayed until `Present()` — so only the
// backbuffer route leaves the queue alive across the destructor. That distinction is why the
// pre-existing lifetime leg of `rendertarget_effect_source_test.cpp` did not catch this: it
// destroys its source AFTER unbinding the destination, so on this renderer the flush had already
// consumed the command. Every dead-source leg here therefore states which destination it uses.
//
// THE FIX resolves the view ONCE, at the public draw call, into a `WebGPUSampledTextureEXT` — the
// already-resolved `WGPUTextureView` plus one native reference on it and its texture. Replay never
// dereferences a wrapper again, and the handles stay valid until the last command carrying them is
// gone. `wgpuTextureRelease`/`wgpuTextureViewRelease` are refcount decrements, not the destructive
// `wgpuTextureDestroy`, so a held reference genuinely keeps the resource usable.
//
// THE ORACLE is pixels, never "did not crash". Every consumer leg reads its destination back and
// compares it to the exact producer pattern, so a renderer that silently DROPS a queued draw whose
// source died (rather than crashing on it) fails just as loudly. Point sampling and a pattern whose
// every channel is a multiple of 5 away from 0 and 255 make each comparison byte-exact.
//
// PROCESS ISOLATION: the pre-fix failure is a SIGSEGV, not an assertion, so one bad leg would take
// down the whole CTest shard and report nothing about the legs behind it. Run with no arguments
// this binary is a SUPERVISOR: it re-execs itself once per leg as `--leg=<id>` and classifies each
// child as PASS / FAIL / CRASH-with-the-signal-named / TIMEOUT. Post-fix nothing crashes and every
// child exits 0; `--leg=<id>` still runs exactly one leg in-process for debugging.
//
// LEGS
//   A1 live control            source alive across the whole frame, backbuffer destination
//   A2 dead Texture2D          an ordinary Texture2D destroyed while its draw is only queued
//   A3 dead RenderTarget2D     THE FINDING -- a render target destroyed while its draw is queued
//   B1 destination flushed     the same, with a render-target destination (flush before the death)
//   B2 death before the flush  render-target destination, source destroyed BEFORE the unbind
//   C1 destruction order       destination before source, and source before destination
//   D1 SpriteBatch consumer    the sprite route's own deferred command, dead source
//   E1 cube                    a RenderTargetCube destroyed while an EnvironmentMapEffect draw waits
//   F1 repetition              create/use/destroy rounds; native handle reuse must not alias
//   G1 two pairs               two independent producer/consumer pairs with interleaved lifetimes
//   H1 unwinding               a throwing consumer, then a clean frame that must still render
//   I1 multi-frame             several frames, each destroying its own source before Present
//   J1 MSAA                    an applied multisample producer, destroyed while its draw is queued
//   K1 exit after a dead frame  the process exits immediately behind a replay that bound a
//                              resource whose public wrapper was already destroyed
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
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX167_CAN_FORK 1
#else
#define CNA_GFX167_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kPW = 8;    ///< producer pattern width  -- non-square on purpose
    constexpr int kPH = 4;    ///< producer pattern height
    /// Backbuffer extent. Each source texel must cover an ODD number of destination pixels, so that
    /// the middle pixel of a block lands EXACTLY on that texel's centre: with an even block, the
    /// centre falls between two pixels and a renderer using linear filtering returns a slight blend
    /// of two neighbours instead of the texel itself. 9 pixels per texel makes the probe exact
    /// under point AND linear sampling, so the comparison stays byte-exact everywhere rather than
    /// needing a tolerance that would also admit a genuinely wrong resource.
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
#elif defined(CNA_RENDERER_LLGL)
    constexpr const char* kRendererName = "LLGL";
#else
#error "REMED-GFX-167: this renderer has no declared deferred-source lifetime contract."
#endif

    /**
     * @brief Whether a `RenderTargetCube` can be rendered into and then sampled as an env map.
     *
     * SDL_RENDERER has no 3D pipeline at all, and HEADLESS does not rasterize. Both are declared
     * rather than asserted, exactly as the neighbouring render-target fixtures already do.
     */
    constexpr bool kRenderTargetCubeSupported =
#if defined(CNA_RENDERER_SDL_RENDERER) || defined(CNA_RENDERER_HEADLESS)
        false;
#else
        true;
#endif

    /**
     * @brief The producer pattern texel at (@p x, @p y), (0, 0) being the TOP-LEFT corner.
     *
     * R is a function of x alone and G of y alone, so the pair (R, G) is unique across all 32
     * texels and identifies both axes independently. B mixes the two, so a 180-degree rotation is
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
     * @brief A second, unmistakably different pattern, for legs that hold two producers at once.
     *
     * Its R decreases with x where PatternColor's increases, so a leg that samples the wrong one of
     * two live sources fails on every texel rather than on a few.
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
 * @brief REMED-GFX-167's public contract: a queued draw outliving its source is safe and correct.
 */
class DeferredSourceLifetimeTest : public Game
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
    /// Leg L1's source, held across a frame boundary so its release lands AFTER a submit.
    std::unique_ptr<RenderTarget2D> inFlight_;

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

    /**
     * @brief Reads the kBBW x kBBH backbuffer through the rectangle-LESS overload.
     *
     * REMED-GFX-165 is fixed: a rectangle-less GetBackBufferData now sizes its region from the
     * authoritative PresentationParameters backbuffer, not the renderer's live viewport, so a
     * correctly-sized W*H array reads the whole backbuffer on WebGPU and SDL_GPU too. This used to
     * name an explicit rectangle to avoid that defect; the workaround has been removed now that the
     * overload it worked around is correct.
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

    /// Asserts an 8x4-texel image reproduces @p want exactly, naming the failure shape rather than
    /// only a count -- an all-poison result (a renderer that wrote nothing) reads differently from
    /// an all-zero one (a renderer that DROPPED the queued draw), and the two mean different things.
    void CheckExact(const Readback& r, const std::string& label, PatternFn want)
    {
        int good = 0, poison = 0, zero = 0;
        std::string firstMismatch;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                // The middle pixel of the block this source texel occupies. Both destination sizes
                // this fixture uses give an odd block (1 for a render target, kBBBlock for the
                // backbuffer), so this pixel's texture coordinate is exactly the texel centre and
                // the comparison is byte-exact whatever the renderer filters with.
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
        else if (zero == total - good && zero > 0) shape = " [queued draw DROPPED, not replayed]";
        check(good == total, label + " (" + std::to_string(good) + "/" + std::to_string(total) + ")" +
                             (good == total ? "" : shape + firstMismatch));
    }

    /**
     * @brief Requires two readbacks of the same sequence to be byte-identical, and NON-TRIVIAL.
     *
     * The oracle for a consumer whose exact output colour this fixture cannot predict -- the
     * env-map families combine the sampled value with lighting and Fresnel terms. Running the
     * identical sequence twice, differing ONLY in when the source is destroyed, turns "is the
     * colour right" into "does the disposal change anything", which is the actual subject and needs
     * no shading model at all.
     *
     * @p live must also carry real content: at least a quarter of its probed pixels differing from
     * the opaque-black clear, so that a renderer which renders nothing in BOTH runs -- the exact
     * failure this is watching for -- cannot pass by being equally empty twice.
     */
    void CheckSameAndLively(const Readback& live, const Readback& dead, const std::string& label)
    {
        int differing = 0, nonBlack = 0;
        std::string firstDiff;
        for (int y = 0; y < kPH; ++y)
            for (int x = 0; x < kPW; ++x)
            {
                const int px = (x * live.w) / kPW + (live.w / kPW) / 2;
                const int py = (y * live.h) / kPH + (live.h / kPH) / 2;
                const Color& l = live.at(px, py);
                const Color& d = dead.at(px, py);
                if (!Same(l, Color(0, 0, 0, 255))) ++nonBlack;
                if (Same(l, d)) continue;
                ++differing;
                if (firstDiff.empty())
                    firstDiff = " first (" + std::to_string(x) + "," + std::to_string(y) +
                                ") live " + ColorText(l) + " dead " + ColorText(d);
            }
        const int total = kPW * kPH;
        if (nonBlack < total / 4)
        {
            check(false, label + ": the LIVE control rendered nothing to compare against (" +
                         std::to_string(nonBlack) + "/" + std::to_string(total) +
                         " non-black) -- this leg would measure nothing");
            return;
        }
        check(differing == 0, label + " (" + std::to_string(total - differing) + "/" +
                              std::to_string(total) + " identical to the live control)" +
                              (differing == 0 ? "" : firstDiff));
    }

    // ---------------------------------------------------------------- producers

    static void FillQuad(VertexPositionTexture* q)
    {
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, Vector2(0.f, 0.f) }; q[1] = { bl, Vector2(0.f, 1.f) }; q[2] = { br, Vector2(1.f, 1.f) };
        q[3] = { tl, Vector2(0.f, 0.f) }; q[4] = { br, Vector2(1.f, 1.f) }; q[5] = { tr, Vector2(1.f, 0.f) };
    }

    /// The same quad carrying normals, so its stream is stride 32 -- the only stride that selects
    /// the EnvironmentMapEffect family, and therefore the only one that binds a cube slot at all.
    static void FillQuadWithNormals(VertexPositionNormalTexture* q)
    {
        const Vector3 n(0.f, 0.f, 1.f);
        const Vector3 tl(-1.f, 1.f, 0.f), bl(-1.f, -1.f, 0.f), br(1.f, -1.f, 0.f), tr(1.f, 1.f, 0.f);
        q[0] = { tl, n, Vector2(0.f, 0.f) }; q[1] = { bl, n, Vector2(0.f, 1.f) };
        q[2] = { br, n, Vector2(1.f, 1.f) }; q[3] = { tl, n, Vector2(0.f, 0.f) };
        q[4] = { br, n, Vector2(1.f, 1.f) }; q[5] = { tr, n, Vector2(1.f, 0.f) };
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

    /** @brief Fills @p rt with @p source's pattern and unbinds it. @p rt is never read back here. */
    void ProduceInto(GraphicsDevice& dev, RenderTarget2D& rt, const Texture2D& source)
    {
        dev.SetRenderTarget(&rt);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        Blit(dev, source, rt.getWidthProperty(), rt.getHeightProperty());
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

    // ---------------------------------------------------------------- consumers

    /**
     * @brief Queues ONE stock-3D draw sampling @p src across the whole current destination.
     *
     * Configured so the output is the sampled texel unchanged: unit diffuse, no lighting, no vertex
     * colour. On a renderer that binds the wrong resource — or replays a freed one — this fails on
     * colour rather than on a tolerance.
     */
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

    /**
     * @brief Consume3D, but through a caller-owned effect, so destruction ORDER can be chosen.
     *
     * Consume3D destroys its BasicEffect at the end of its own scope, i.e. always BEFORE the source.
     * Leg N1 needs the other order too.
     */
    static void Consume3DWith(GraphicsDevice& dev, BasicEffect& fx, Texture2D* src)
    {
        VertexPositionTexture q[6];
        FillQuad(q);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(src);
        fx.VertexColorEnabled = false;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    /** @brief Queues ONE SpriteBatch draw of @p src across the whole current destination. */
    void ConsumeSprite(GraphicsDevice& dev, Texture2D& src, int w, int h)
    {
        Blit(dev, src, w, h);
    }

    // ================================================================ legs

    /** @brief Leg A1 -- the live control: nothing is destroyed early, so this must always pass. */
    void LegA1(GraphicsDevice& dev)
    {
        RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, a, patternTex_);
        BeginBackbuffer(dev);
        Consume3D(dev, &a);
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "A1"))
            CheckExact(r, "A1 a LIVE render-target source is sampled onto the backbuffer",
                       PatternColor);
    }

    /** @brief Leg A2 -- an ordinary Texture2D destroyed while its backbuffer draw is only queued. */
    void LegA2(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        {
            std::vector<std::uint8_t> px(static_cast<std::size_t>(kPW) * kPH * 4);
            for (int y = 0; y < kPH; ++y)
                for (int x = 0; x < kPW; ++x)
                {
                    const Color c = PatternColor(x, y);
                    const std::size_t o = (static_cast<std::size_t>(y) * kPW + x) * 4;
                    px[o + 0] = c.getRProperty(); px[o + 1] = c.getGProperty();
                    px[o + 2] = c.getBProperty(); px[o + 3] = c.getAProperty();
                }
            Texture2D shortLived = Texture2D::CreateFromPixels(dev, kPW, kPH, px);
            Consume3D(dev, &shortLived);
        }   // <-- destroyed with its consumer draw still only QUEUED for the backbuffer
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "A2"))
            CheckExact(r, "A2 a Texture2D destroyed before Present is still sampled correctly",
                       PatternColor);
    }

    /**
     * @brief Leg A3 -- THE FINDING. A render target destroyed while its backbuffer draw is queued.
     *
     * This is the exact sequence that reproduced the SIGSEGV: no flush happens between the consumer
     * draw and `a`'s destructor, so the queued command still carried a pointer to a freed renderer
     * when `Present()` replayed it.
     */
    void LegA3(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &a);
        }   // <-- destroyed with its consumer draw still only QUEUED for the backbuffer
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "A3"))
            CheckExact(r, "A3 a RenderTarget2D destroyed before Present is still sampled correctly",
                       PatternColor);
    }

    /**
     * @brief Leg B1 -- render-target destination, source destroyed AFTER the unbind flushed it.
     *
     * "After the flush" is only true for a renderer that renders a render-target destination at
     * `SetRenderTarget()`. A WHOLE-FRAME-deferred recorder holds even this draw until Present, so
     * for it B1 is a dead-source case like the rest and belongs behind the same declaration.
     */
    void LegB1(GraphicsDevice& dev)
    {
        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            Consume3D(dev, &a);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
        }   // <-- a renderer that flushes at unbind has already consumed the draw here
        Readback r = ReadWholeTarget(dst, kPW, kPH);
        if (Readable(r, "B1"))
            CheckExact(r, "B1 source destroyed after the destination unbind is sampled correctly",
                       PatternColor);
    }

    /**
     * @brief Leg B2 -- render-target destination, source destroyed BEFORE the unbind.
     *
     * B1's ordering lets a flush-at-unbind renderer consume the draw while the source is still
     * alive, which is exactly how this defect stayed invisible in the pre-existing lifetime leg.
     * Here the destructor runs first, so the queue outlives the source on EVERY renderer.
     */
    void LegB2(GraphicsDevice& dev)
    {
        RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::DiscardContents);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            dev.SetRenderTarget(&dst);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            Consume3D(dev, &a);
        }   // <-- destroyed while dst is STILL BOUND, so no flush can have run yet
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
        Readback r = ReadWholeTarget(dst, kPW, kPH);
        if (Readable(r, "B2"))
            CheckExact(r, "B2 source destroyed while the destination is still bound is sampled "
                          "correctly", PatternColor);
    }

    /** @brief Leg C1 -- both destruction orders of a producer/consumer pair. */
    void LegC1(GraphicsDevice& dev)
    {
        // Destination destroyed BEFORE the source.
        {
            auto dst = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0,
                                                        RenderTargetUsage::DiscardContents);
            auto src = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0,
                                                        RenderTargetUsage::DiscardContents);
            ProduceInto(dev, *src, patternTex_);
            dev.SetRenderTarget(dst.get());
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            Consume3D(dev, src.get());
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);
            dst.reset();
            src.reset();
        }
        check(true, "C1 destination destroyed before its source completes without terminating");

        // Source destroyed BEFORE the destination, then the destination is still readable.
        {
            RenderTarget2D dst(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            {
                RenderTarget2D src(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                   RenderTargetUsage::DiscardContents);
                ProduceInto(dev, src, patternTex_);
                dev.SetRenderTarget(&dst);
                ResetState(dev);
                dev.Clear(Color(0, 0, 0, 255));
                Consume3D(dev, &src);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
            }
            // Same reasoning as leg B1: on a whole-frame-deferred recorder this destructor also
            // runs before the draw renders, so the pixel half is declared there rather than here.
            Readback r = ReadWholeTarget(dst, kPW, kPH);
            if (Readable(r, "C1"))
                CheckExact(r, "C1 source destroyed before its destination leaves the destination "
                              "correct", PatternColor);
        }
    }

    /** @brief Leg D1 -- the SpriteBatch route's own deferred command, with a dead source. */
    void LegD1(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            ConsumeSprite(dev, a, kBBW, kBBH);
        }   // <-- destroyed with its SpriteBatch draw still only QUEUED
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "D1"))
            CheckExact(r, "D1 a SpriteBatch source destroyed before Present is still sampled "
                          "correctly", PatternColor);
    }

    /** @brief Leg E1 -- a RenderTargetCube destroyed while its EnvironmentMapEffect draw waits. */
    void LegE1(GraphicsDevice& dev)
    {
        if (!kRenderTargetCubeSupported)
        {
            boundary(std::string("E1 ") + kRendererName + " has no render-into-a-cube path -- "
                     "declared, not measured");
            return;
        }
        // The subject is the cube slot's LIFETIME, not its colour maths, so the check is that the
        // sequence completes and the process survives the replay. E1's sibling legs carry the
        // pixel oracle for the 2D slot, which shares the very same resolver.
        bool threw = false;
        std::string what;
        try
        {
            BeginBackbuffer(dev);
            {
                RenderTargetCube cube(dev, 4, false, SurfaceFormat::Color, DepthFormat::None);
                for (int face = 0; face < 6; ++face)
                {
                    dev.SetRenderTarget(&cube, static_cast<CubeMapFace>(face));
                    ResetState(dev);
                    dev.Clear(Color(30 + face * 30, 90, 220, 255));
                }
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                BeginBackbuffer(dev);

                // Stride 32 (VertexPositionNormalTexture) is what actually selects the env-map
                // family. A stride-20 VertexPositionTexture quad is silently routed to the plain
                // textured family instead, which never binds the cube slot at all -- so this leg
                // would "pass" while measuring nothing. Measured, not assumed: with stride 20 this
                // leg passed even against the unfixed renderer.
                VertexPositionNormalTexture q[6];
                FillQuadWithNormals(q);
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&patternTex_);
                fx.setEnvironmentMapProperty(&cube);
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.f, 0.f, 0.f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            }   // <-- the cube is destroyed with its consumer draw still only QUEUED
            Readback r = ReadBackbuffer(dev);
            (void)r;
        }
        catch (const System::NotSupportedException& e)
        {
            // A renderer without a render-into-a-cube path says so through the public API. That is a
            // declared capability boundary, not a lifetime result -- recorded rather than counted,
            // so this leg can never pass by quietly catching a real failure either.
            boundary(std::string("E1 ") + kRendererName + " has no RenderTargetCube path (" +
                     e.what() + ") -- declared, not measured");
            return;
        }
        catch (const std::exception& e) { threw = true; what = e.what(); }
        catch (...)                     { threw = true; what = "(non-std exception)"; }
        check(!threw, std::string("E1 a RenderTargetCube destroyed before Present replays safely") +
                      (threw ? ": threw " + what : ""));
    }

    /**
     * @brief Leg F1 -- create/use/destroy rounds. Native handle reuse must not alias.
     *
     * Each round's source carries a DIFFERENT pattern, so a round that reads the previous round's
     * recycled handle fails on colour instead of quietly passing.
     */
    void LegF1(GraphicsDevice& dev)
    {
        for (int round = 0; round < 4; ++round)
        {
            PatternFn want = (round % 2 == 0) ? &PatternColor : &AltPatternColor;
            BeginBackbuffer(dev);
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, (round % 2 == 0) ? patternTex_ : altPatternTex_);
                BeginBackbuffer(dev);
                Consume3D(dev, &a);
            }
            Readback r = ReadBackbuffer(dev);
            if (!Readable(r, "F1")) return;
            CheckExact(r, "F1 create/use/destroy round " + std::to_string(round) +
                          " reads its OWN pattern (handle reuse is safe)", want);
        }
    }

    /** @brief Leg G1 -- two independent pairs, one destroyed early and one kept alive. */
    void LegG1(GraphicsDevice& dev)
    {
        RenderTarget2D survivor(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
        ProduceInto(dev, survivor, altPatternTex_);

        BeginBackbuffer(dev);
        {
            RenderTarget2D doomed(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            ProduceInto(dev, doomed, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &doomed);
        }   // <-- one of the two dies; the other must be entirely undisturbed
        Readback r1 = ReadBackbuffer(dev);
        if (!Readable(r1, "G1")) return;
        CheckExact(r1, "G1 interleaved lifetimes: the destroyed source's draw is correct",
                   PatternColor);

        BeginBackbuffer(dev);
        Consume3D(dev, &survivor);
        Readback r2 = ReadBackbuffer(dev);
        if (!Readable(r2, "G1")) return;
        CheckExact(r2, "G1 interleaved lifetimes: the surviving source is undisturbed",
                   AltPatternColor);
    }

    /** @brief Leg H1 -- a throwing consumer must leave nothing dangling for the next frame. */
    void LegH1(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        try
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &a);
            throw std::runtime_error("REMED-GFX-167 deliberate unwind");
        }
        catch (const std::runtime_error&)
        {
            // `a` was destroyed by the unwind, with its consumer draw still queued.
        }
        // A clean frame after the unwind must still render correctly.
        RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, b, altPatternTex_);
        BeginBackbuffer(dev);
        Consume3D(dev, &b);
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "H1"))
            CheckExact(r, "H1 a draw after an exception unwound a queued source is still correct",
                       AltPatternColor);
    }

    /** @brief Leg J1 -- an applied multisample producer destroyed while its draw is queued. */
    void LegJ1(GraphicsDevice& dev)
    {
        RenderTarget2D probe(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
        const int applied = probe.getMultiSampleCountProperty();
        if (applied <= 0)
        {
            boundary(std::string("J1 ") + kRendererName + " applied multisample count " +
                     std::to_string(applied) + " for a requested 4 -- no MSAA resolve to measure");
            return;
        }
        BeginBackbuffer(dev);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 4,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &a);
        }
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "J1"))
            CheckExact(r, "J1 a multisampled source destroyed before Present binds its RESOLVED "
                          "texture", PatternColor);
    }

    /**
     * @brief Leg K1 -- the process exits straight after a frame that sampled an already-dead source.
     *
     * Every other backbuffer leg forces the replay itself, by reading the destination back. This
     * one deliberately does NOT: the consumer draw is left for the frame's own Present, and the leg
     * returns, so device teardown follows immediately behind a replay that bound a resource whose
     * public wrapper is already gone. The subject is the process outcome, which the supervisor
     * judges — the pixel oracle lives in the legs that can still read their destination.
     *
     * Deliberately NOT written as "leave a render target bound and let it be destroyed": that
     * conflates this subject with a separate defect (destroying a RenderTarget2D while it is still
     * the bound target faults inside the next SetRenderTarget on EASYGL — recorded independently,
     * not this task's).
     */
    void LegK1(GraphicsDevice& dev)
    {
        BeginBackbuffer(dev);
        {
            RenderTarget2D src(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                               RenderTargetUsage::DiscardContents);
            ProduceInto(dev, src, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &src);
        }   // <-- source gone; nothing here forces the replay, so Present does it on the way out
        check(true, "K1 exiting straight after a frame that sampled a dead source completes");
    }

    /**
     * @brief Leg E2 -- the cube slot with a PIXEL oracle, not just "replays safely".
     *
     * E1 only asserts that the sequence completes, because this fixture cannot predict
     * EnvironmentMapEffect's combined output colour. E2 does not need to: it runs the identical
     * sequence twice, differing ONLY in whether the cube survives to the frame, and requires the two
     * destinations to be byte-identical AND the live one to be non-trivial. A dropped cube-face
     * producer changes the sampled environment, so it cannot pass unnoticed.
     *
     * EnvironmentMapAmount is 1, not E1's 0: with 0 the cube contributes nothing and the comparison
     * would be vacuous whatever the lifetime did.
     */
    void LegE2(GraphicsDevice& dev)
    {
        if (!kRenderTargetCubeSupported)
        {
            boundary(std::string("E2 ") + kRendererName + " has no render-into-a-cube path -- "
                     "declared, not measured");
            return;
        }
        Readback reads[2];
        for (int side = 0; side < 2; ++side)   // side 0 = cube destroyed early, 1 = cube alive
        {
            std::unique_ptr<RenderTargetCube> cube;
            try
            {
                BeginBackbuffer(dev);
                cube = std::make_unique<RenderTargetCube>(dev, 4, false, SurfaceFormat::Color,
                                                          DepthFormat::None);
                for (int face = 0; face < 6; ++face)
                {
                    dev.SetRenderTarget(cube.get(), static_cast<CubeMapFace>(face));
                    ResetState(dev);
                    dev.Clear(Color(static_cast<bytecs>(30 + face * 30), bytecs(90), bytecs(220),
                                    bytecs(255)));
                }
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                ResetState(dev);
                BeginBackbuffer(dev);

                VertexPositionNormalTexture q[6];
                FillQuadWithNormals(q);
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&patternTex_);
                fx.setEnvironmentMapProperty(cube.get());
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3(0.f, 0.f, 0.f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
            }
            catch (const System::NotSupportedException& e)
            {
                boundary(std::string("E2 ") + kRendererName + " has no RenderTargetCube path (" +
                         e.what() + ") -- declared, not measured");
                return;
            }
            if (side == 0) cube.reset();   // <-- destroyed with its consumer draw still QUEUED
            reads[side] = ReadBackbuffer(dev);
            if (!Readable(reads[side], "E2")) return;
        }
        CheckSameAndLively(reads[1], reads[0],
                           "E2 a RenderTargetCube destroyed before Present is sampled identically "
                           "to a live one");
    }

    /**
     * @brief Leg L1 -- the source is destroyed AFTER its frame was submitted, in the NEXT frame.
     *
     * Every other leg's disposal happens before the record. This one happens after it: frame 1
     * queues producer and consumer and ends, so Present submits them; frame 2 then destroys the
     * render target while that submission may still be executing (Vulkan keeps several frames in
     * flight), and renders a fresh pair whose result must be exact. The subject is the GPU half of
     * the ownership rule -- native storage must outlive submitted work, not merely the CPU record.
     */
    void LegL1(GraphicsDevice& dev, int frame)
    {
        if (frame == 1)
        {
            BeginBackbuffer(dev);
            inFlight_ = std::make_unique<RenderTarget2D>(dev, kPW, kPH, false, SurfaceFormat::Color,
                                                         DepthFormat::None, 0,
                                                         RenderTargetUsage::DiscardContents);
            ProduceInto(dev, *inFlight_, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, inFlight_.get());
            return;   // <-- Present submits this frame with inFlight_ still alive
        }
        if (frame == 2)
        {
            // Submitted, quite possibly still executing, and now released by the game.
            inFlight_.reset();
            BeginBackbuffer(dev);
            RenderTarget2D b(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, b, altPatternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &b);
            Readback r = ReadBackbuffer(dev);
            if (Readable(r, "L1"))
                CheckExact(r, "L1 the frame after a submitted frame's source was released is exact",
                           AltPatternColor);
            return;
        }
        BeginBackbuffer(dev);
        RenderTarget2D c(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                         RenderTargetUsage::DiscardContents);
        ProduceInto(dev, c, patternTex_);
        BeginBackbuffer(dev);
        Consume3D(dev, &c);
        Readback r = ReadBackbuffer(dev);
        if (Readable(r, "L1"))
            CheckExact(r, "L1 a third frame after an in-flight release is still exact", PatternColor);
    }

    /**
     * @brief Leg N1 -- both destruction orders of the consumer's EFFECT and its source.
     *
     * Every other leg destroys the effect first, because Consume3D owns it in its own scope. A
     * lifetime fix that made a queued command depend on its effect object outliving its source (or
     * the reverse) would only show up here.
     */
    void LegN1(GraphicsDevice& dev)
    {
        // (a) EFFECT destroyed first, source second.
        BeginBackbuffer(dev);
        {
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            {
                BasicEffect fx(dev);
                Consume3DWith(dev, fx, &a);
            }   // effect gone
        }       // source gone
        Readback ra = ReadBackbuffer(dev);
        if (Readable(ra, "N1"))
            CheckExact(ra, "N1 effect destroyed before its source leaves the queued draw correct",
                       PatternColor);

        // (b) SOURCE destroyed first, effect second.
        BeginBackbuffer(dev);
        {
            BasicEffect fx(dev);
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, altPatternTex_);
                BeginBackbuffer(dev);
                Consume3DWith(dev, fx, &a);
            }   // source gone first
        }       // effect gone second
        Readback rb = ReadBackbuffer(dev);
        if (Readable(rb, "N1"))
            CheckExact(rb, "N1 source destroyed before its effect leaves the queued draw correct",
                       AltPatternColor);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        ++frame_;

        // Leg I1's subject is that SEVERAL public frames each destroy their own source before their
        // own Present. Frames 1 and 2 queue-and-destroy; the check runs in frame 3.
        // Leg L1's subject spans frames: frame 1 queues and is submitted, frame 2 releases the
        // source while that submission may still be executing, frame 3 confirms recovery.
        if (wants("L1"))
        {
            LegL1(dev, frame_);
            if (frame_ < 3) return;
            std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
            std::fflush(stdout);
            result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
            Exit();
            return;
        }

        if (wants("I1") && frame_ <= 2)
        {
            BeginBackbuffer(dev);
            RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::DiscardContents);
            ProduceInto(dev, a, patternTex_);
            BeginBackbuffer(dev);
            Consume3D(dev, &a);
            return;   // <-- `a` dies here, and this frame's Present replays its draw
        }

        std::printf("[INFO] REMED-GFX-167 deferred-source lifetime on %s (%dx%d pattern%s)\n",
                    kRendererName, kPW, kPH,
                    onlyLeg_.empty() ? "" : (", leg " + onlyLeg_).c_str());
        std::fflush(stdout);

        auto runLeg = [&](const char* id, void (DeferredSourceLifetimeTest::*leg)(GraphicsDevice&))
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
            // A leg that threw mid-bind must not leave a target bound for the next one.
            try { dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); } catch (...) {}
        };

        runLeg("A1", &DeferredSourceLifetimeTest::LegA1);
        runLeg("A2", &DeferredSourceLifetimeTest::LegA2);
        runLeg("A3", &DeferredSourceLifetimeTest::LegA3);
        runLeg("B1", &DeferredSourceLifetimeTest::LegB1);
        runLeg("B2", &DeferredSourceLifetimeTest::LegB2);
        runLeg("C1", &DeferredSourceLifetimeTest::LegC1);
        runLeg("D1", &DeferredSourceLifetimeTest::LegD1);
        runLeg("E1", &DeferredSourceLifetimeTest::LegE1);
        runLeg("E2", &DeferredSourceLifetimeTest::LegE2);
        runLeg("F1", &DeferredSourceLifetimeTest::LegF1);
        runLeg("G1", &DeferredSourceLifetimeTest::LegG1);
        runLeg("H1", &DeferredSourceLifetimeTest::LegH1);
        runLeg("J1", &DeferredSourceLifetimeTest::LegJ1);
        runLeg("K1", &DeferredSourceLifetimeTest::LegK1);
        runLeg("N1", &DeferredSourceLifetimeTest::LegN1);

        if (wants("I1"))
        {
            BeginBackbuffer(dev);
            {
                RenderTarget2D a(dev, kPW, kPH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                 RenderTargetUsage::DiscardContents);
                ProduceInto(dev, a, patternTex_);
                BeginBackbuffer(dev);
                Consume3D(dev, &a);
            }
            Readback r = ReadBackbuffer(dev);
            if (Readable(r, "I1"))
                CheckExact(r, "I1 the third consecutive frame to destroy its own source is "
                              "still correct", PatternColor);
        }

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
    explicit DeferredSourceLifetimeTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        // Leg J1's subject is a MULTISAMPLED source, and on renderers that tie render-target MSAA to
        // the device's own sample count (Vulkan does) a render target can only engage it when the
        // backbuffer itself was created multisampled. Requested ONLY in J1's own child process --
        // the supervisor runs one leg per child -- so no other leg's measurement moves, and the
        // in-process fallback keeps J1's honest "nothing to measure" declaration.
        if (onlyLeg_ == "J1") gdm_->setPreferMultiSamplingProperty(true);
    }

    /** @brief 0 when every check passed, 1 otherwise. */
    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    /** @brief Every leg id this fixture knows, in the order the supervisor runs them. */
    const char* const kLegIds[] = {
        "A1", "A2", "A3", "B1", "B2", "C1", "D1", "E1", "E2", "F1", "G1", "H1", "I1", "J1",
        "K1", "L1", "N1",
    };

#if CNA_GFX167_CAN_FORK
    /// A leg that hangs must be reported as a TIMEOUT, not waited on forever -- a modal dialog or a
    /// wedged device is a different finding from a crash and has to stay distinguishable.
    constexpr unsigned kLegTimeoutSeconds = 180;

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

#if CNA_GFX167_CAN_FORK
    if (onlyLeg.empty())
    {
        // Supervisor. Each leg runs in its own child so a SIGSEGV is a reported result rather than
        // the end of the run.
        std::printf("[INFO] REMED-GFX-167 supervisor: %zu legs, each in its own process\n",
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

    DeferredSourceLifetimeTest game(onlyLeg);
    game.Run();
    return game.Result();
}
