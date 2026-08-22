// SPDX-License-Identifier: MS-PL
//
// CNAEXT. A renderer-agnostic sprite benchmark, deliberately written against nothing but the public
// XNA API (no CNA::Internal::Renderers::* include, no renderer-specific hook) so the identical
// source produces a genuine like-for-like comparison. In browsers it retains the original
// CANVAS/EasyGL/HTML_DOM reporting contract. On native hosts it is also the PLAT-7/PLAT-120 fixed
// scene: build the target once per selected renderer and compare its raw per-frame JSON samples.
//
// plans/plan_html_dom.md HTMLDOM-111 (reopens HTMLDOM-89/92/99's own conclusions): an earlier version of
// this file (and htmldom_stress_test.cpp's own HTMLDOM-89 benchmark) stopped its timer immediately
// after SpriteBatch::End(), before Present() and before Chromium's own deferred style/layout/
// paint/compositor work -- structurally favouring DOM style submission (cheap, deferred) over
// Canvas2D/WebGL work (whose real cost is closer to synchronous), and unable to support an
// "fps-equivalent" or cross-renderer "X is faster" claim at all. Two independent corrections:
//
// 1. TWO metrics, not one, both reported: "CPU submission" (the original performance.now() bracket
//    around Begin()/Draw()/End() -- real, but explicitly NOT a frame-rate number) and "real
//    end-to-end frame cadence", measured as the wall-clock gap between the START of one Draw() call
//    and the START of the next. Game::Run() awaits requestAnimationFrame() through Asyncify while
//    retaining its Wasm caller, so this interval includes the previous frame's browser-visible
//    scheduling delay and is the one thing the old submission-only bracket could never see.
// 2. TWO workloads, not one: "stable tint" (position animates every frame, tint per sprite is FIXED
//    -- this renderer's own documented sweet spot, "rewards static sprite sheets and moving
//    sprites") and "heavy churn" (position AND tint both animate every frame -- this renderer's own
//    documented weak point, "a new cached variant per distinct colour"). The ORIGINAL single-
//    workload version of this benchmark measured ONLY the heavy-churn case while calling it
//    "steady state" -- a real, if unintentional, apples-to-oranges mislabelling: the published
//    2.338 ms/frame number was this renderer's OWN worst documented case, not its best one.
//
// Also captures navigator.userAgent (browser/build identification) and a real
// WEBGL_debug_renderer_info UNMASKED_RENDERER_WEBGL query -- via a throwaway canvas unrelated to
// whichever renderer actually built this binary, since any WebGL context in the same browser
// session reports the same renderer string -- so a hardware GPU result is never silently compared
// against a software (SwiftShader) one without saying so. Harmless, informational-only for
// CANVAS/HTML_DOM (neither creates a WebGL context of its own at all); genuinely load-bearing for
// EASYGL, whose own canvas IS exactly what this ends up querying.
//
// Publishes raw per-frame samples (window.__cnaBenchSamples, a JSON array) alongside the averaged
// numbers, so a result is independently reproducible/re-analysable rather than only the single
// averaged headline number.
//
// Not registered as a CTest -- like every other Emscripten-only browser page in this project, it
// needs a real browser (headless Chromium via Playwright), not `node`.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// The only "which renderer am I" this file needs -- a compile-time constant already computed from
// the CNA_RENDERER_* define RendererSelection.cmake sets, the same value every renderer's own
// getCurrentGraphicsRendererName() call already reports elsewhere in this project. Using it instead
// of inventing a fresh per-benchmark compile definition keeps this file genuinely renderer-agnostic
// source -- nothing about it changes per renderer except which CNA_GRAPHICS_RENDERER the CMake
// configure step that builds it selected.
#include "CNA/GraphicsRendererType.hpp"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSpriteCount = 500;

#if defined(__EMSCRIPTEN__)
    constexpr int kWarmupFrames = 1;
    constexpr int kDefaultPhaseFrames = 60;
#else
    constexpr int kWarmupFrames = 120;
    constexpr int kDefaultPhaseFrames = 1200;
#endif

    int PhaseFrames()
    {
#if defined(__EMSCRIPTEN__)
        return kDefaultPhaseFrames;
#else
        const char* configured = std::getenv("CNA_BENCH_PHASE_FRAMES");
        if (configured == nullptr)
            return kDefaultPhaseFrames;

        const long value = std::strtol(configured, nullptr, 10);
        return value >= 100 && value <= 100000 ? static_cast<int>(value) : kDefaultPhaseFrames;
#endif
    }

#if defined(__EMSCRIPTEN__)
    EM_JS(double, JsNow, (), { return performance.now(); });

    // plans/plan_html_dom.md HTMLDOM-111: raw per-frame end-to-end samples for the phase currently
    // measuring, so the published average is independently re-derivable, not just asserted.
    EM_JS(void, JsPushSample, (const char* phase, double ms), {
        if (!window.__cnaBenchSamples) window.__cnaBenchSamples = [];
        window.__cnaBenchSamples.push({ phase: UTF8ToString(phase), ms: ms });
    });

    EM_JS(void, JsPublishResult, (const char* rendererName,
                                  double stableSubmissionMs, double stableEndToEndMs,
                                  double churnSubmissionMs, double churnEndToEndMs), {
        window.__cnaBenchRenderer = UTF8ToString(rendererName);
        window.__cnaBenchStableSubmissionMs = stableSubmissionMs;
        window.__cnaBenchStableEndToEndMs = stableEndToEndMs;
        window.__cnaBenchChurnSubmissionMs = churnSubmissionMs;
        window.__cnaBenchChurnEndToEndMs = churnEndToEndMs;
        // Backward-compatible single-number field some earlier tooling may still read -- the
        // heavy-churn end-to-end number, since that is the closer (though still not identical)
        // analogue of what the old single "avgMs" used to mean.
        window.__cnaBenchAvgMs = churnEndToEndMs;
        window.__cnaBenchUserAgent = (typeof navigator !== 'undefined') ? navigator.userAgent : "";
        // plans/plan_html_dom.md HTMLDOM-111: never let a hardware-GPU WebGL result and a software
        // (SwiftShader) one be compared without saying so. A real query, via a throwaway canvas
        // unrelated to whichever CNA renderer actually built this binary -- any WebGL context in
        // the same browser session reports the same renderer string, so this works identically
        // for EASYGL (whose OWN canvas is exactly this) and is simply extra, harmless information
        // for CANVAS/HTML_DOM (which never create a WebGL context of their own at all).
        window.__cnaBenchWebglRenderer = (function () {
            try {
                const c = document.createElement('canvas');
                const gl = c.getContext('webgl2') || c.getContext('webgl');
                if (!gl) return 'no WebGL context available in this browser';
                const ext = gl.getExtension('WEBGL_debug_renderer_info');
                if (!ext) return 'WEBGL_debug_renderer_info unavailable (renderer string hidden)';
                return String(gl.getParameter(ext.UNMASKED_RENDERER_WEBGL));
            } catch (e) {
                return 'WebGL renderer query failed: ' + e;
            }
        })();
        window.__cnaSmokeDone = true;
        window.__cnaSmokeResult = 0;
    });
#else
    double JsNow()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
    }
    void JsPushSample(const char*, double) {}
    void JsPublishResult(const char*, double, double, double, double) {}
#endif

    void PrintJsonArray(const std::vector<double>& samples)
    {
        std::putchar('[');
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            if (i != 0)
                std::putchar(',');
            std::printf("%.9f", samples[i]);
        }
        std::putchar(']');
    }

    void PrintNativeSamples(const std::string& renderer, const char* phase, const char* metric,
                            const std::vector<double>& samples)
    {
#if !defined(__EMSCRIPTEN__)
        std::printf("{\"schema\":1,\"renderer\":\"%s\",\"phase\":\"%s\","
                    "\"metric\":\"%s\",\"sprites\":%d,\"samples_ms\":",
                    renderer.c_str(), phase, metric, kSpriteCount);
        PrintJsonArray(samples);
        std::puts("}");
#else
        (void)renderer;
        (void)phase;
        (void)metric;
        (void)samples;
#endif
    }
}

class GraphicsRendererBenchmark : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    const int phaseFrames_ = PhaseFrames();
    int frame_ = 0;
    double lastFrameStart_ = -1.0;

    double stableSubmissionTotalMs_ = 0.0;
    double stableEndToEndTotalMs_ = 0.0;
    int stableFramesTimed_ = 0;
    std::vector<double> stableSubmissionSamples_;
    std::vector<double> stableEndToEndSamples_;

    double churnSubmissionTotalMs_ = 0.0;
    double churnEndToEndTotalMs_ = 0.0;
    int churnFramesTimed_ = 0;
    std::vector<double> churnSubmissionSamples_;
    std::vector<double> churnEndToEndSamples_;

    // plans/plan_html_dom.md HTMLDOM-111: draws kSpriteCount sprites, all moving every frame (this
    // renderer's own documented sweet spot on the position side regardless of workload). `churnTint`
    // selects which of the two workloads this call belongs to: false = "stable tint" (per-sprite
    // colour is a pure function of `i` alone -- fixed across every frame, the OTHER half of the
    // documented sweet spot); true = "heavy churn" (colour is ALSO a function of `frame_`, a fresh
    // value every single frame, this renderer's own documented weak point).
    void DrawSprites(bool churnTint)
    {
        spriteBatch_->Begin();
        for (int i = 0; i < kSpriteCount; ++i)
        {
            const float x = static_cast<float>((i * 37 + frame_ * 2) % 400);
            const float y = static_cast<float>((i * 53 + frame_) % 200);
            const std::uint8_t r = churnTint
                ? static_cast<std::uint8_t>(std::fmod(static_cast<float>(frame_) * 3.7f + i, 256.0f))
                : static_cast<std::uint8_t>(i % 256);
            spriteBatch_->Draw(*texture_, Vector2(x, y),
                               Color(r, static_cast<std::uint8_t>(200), static_cast<std::uint8_t>(150),
                                     static_cast<std::uint8_t>(255)));
        }
        spriteBatch_->End();
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 255, 255, 255,  255, 255, 255, 255,
                255, 255, 255, 255,  255, 255, 255, 255,
            }));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        const double frameStart = JsNow();
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 20, 255));

        // Native runs use a longer warm-up to settle code/data caches and CPU frequency. Browser
        // runs retain their historical one-frame warm-up. Churn immediately follows stable tint;
        // it intentionally has no cache steady-state because fresh tint values are the workload.
        const int stableEnd = kWarmupFrames + phaseFrames_;
        const int churnEnd = kWarmupFrames + 2 * phaseFrames_;
        const bool inStablePhase = frame_ > kWarmupFrames && frame_ <= stableEnd;
        const bool inChurnPhase = frame_ > stableEnd && frame_ <= churnEnd;

        const double subT0 = (inStablePhase || inChurnPhase) ? JsNow() : 0.0;
        DrawSprites(/*churnTint=*/inChurnPhase);
        if (inStablePhase || inChurnPhase)
        {
            const double subMs = JsNow() - subT0;
            (inStablePhase ? stableSubmissionTotalMs_ : churnSubmissionTotalMs_) += subMs;
            (inStablePhase ? stableSubmissionSamples_ : churnSubmissionSamples_).push_back(subMs);
        }

        // End-to-end: the wall-clock gap since the PREVIOUS Draw() call started -- see this file's
        // own top-of-file comment for why that is exactly the real frame period under Emscripten's
        // requestAnimationFrame-paced main loop. The transition INTO frame 2 (gap = frame 1's own
        // expensive warm-up) and INTO the churn phase's first frame (gap = the stable phase's own
        // last, still-valid frame -- fine to include, but the ACT of crossing phases mid-gap makes
        // that one sample ambiguous) are both excluded by only recording once frame_ > 2 within a
        // phase whose PREVIOUS frame was also in a timed, same-kind window.
        if (lastFrameStart_ >= 0.0)
        {
            const double gapMs = frameStart - lastFrameStart_;
            if (inStablePhase && frame_ > kWarmupFrames + 1)
            {
                stableEndToEndTotalMs_ += gapMs;
                ++stableFramesTimed_;
                stableEndToEndSamples_.push_back(gapMs);
                JsPushSample("stable", gapMs);
            }
            else if (inChurnPhase && frame_ > stableEnd + 1)
            {
                churnEndToEndTotalMs_ += gapMs;
                ++churnFramesTimed_;
                churnEndToEndSamples_.push_back(gapMs);
                JsPushSample("churn", gapMs);
            }
        }
        lastFrameStart_ = frameStart;

        if (frame_ == churnEnd)
        {
            const double stableSubAvg = stableSubmissionTotalMs_ / phaseFrames_;
            const double churnSubAvg = churnSubmissionTotalMs_ / phaseFrames_;
            const double stableE2eAvg =
                stableFramesTimed_ > 0 ? stableEndToEndTotalMs_ / stableFramesTimed_ : -1.0;
            const double churnE2eAvg =
                churnFramesTimed_ > 0 ? churnEndToEndTotalMs_ / churnFramesTimed_ : -1.0;

            const auto rendererName = CNA::getCurrentGraphicsRendererName();
            std::printf("=== [%.*s] %d sprites/frame ===\n",
                        static_cast<int>(rendererName.size()), rendererName.data(), kSpriteCount);
            std::printf("    stable tint : submission %.4f ms/frame | end-to-end %.4f ms/frame "
                        "(%.1f real fps)\n",
                        stableSubAvg, stableE2eAvg, stableE2eAvg > 0 ? 1000.0 / stableE2eAvg : 0.0);
            std::printf("    heavy churn : submission %.4f ms/frame | end-to-end %.4f ms/frame "
                        "(%.1f real fps)\n",
                        churnSubAvg, churnE2eAvg, churnE2eAvg > 0 ? 1000.0 / churnE2eAvg : 0.0);
            std::printf("    (submission = SpriteBatch Begin/Draw/End CPU time only; end-to-end "
                        "= wall-clock gap between successive Draw calls, including the host "
                        "loop's event/update/present work)\n");
            std::fflush(stdout);

            const std::string rendererNameStr(rendererName);
            PrintNativeSamples(rendererNameStr, "stable", "submission", stableSubmissionSamples_);
            PrintNativeSamples(rendererNameStr, "stable", "end_to_end", stableEndToEndSamples_);
            PrintNativeSamples(rendererNameStr, "churn", "submission", churnSubmissionSamples_);
            PrintNativeSamples(rendererNameStr, "churn", "end_to_end", churnEndToEndSamples_);
            JsPublishResult(rendererNameStr.c_str(), stableSubAvg, stableE2eAvg, churnSubAvg, churnE2eAvg);
            Exit();
        }
    }

public:
    GraphicsRendererBenchmark()
    {
        setIsFixedTimeStepProperty(false);
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(480);
        gdm_->setPreferredBackBufferHeightProperty(270);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
        stableSubmissionSamples_.reserve(static_cast<std::size_t>(phaseFrames_));
        stableEndToEndSamples_.reserve(static_cast<std::size_t>(phaseFrames_ - 1));
        churnSubmissionSamples_.reserve(static_cast<std::size_t>(phaseFrames_));
        churnEndToEndSamples_.reserve(static_cast<std::size_t>(phaseFrames_ - 1));
    }
};

int main()
{
    GraphicsRendererBenchmark game;
    game.Run();
    return 0;
}
